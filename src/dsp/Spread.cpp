#include "Spread.h"

#include <algorithm>
#include <cmath>

namespace nsdsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

// Section engage/bypass blend, and the engage/bypass crossfade.
constexpr double kFadeSeconds = 0.025;

// One-pole on the SIGNED offset: sweeps across center pass cleanly through
// identity instead of leaving decaying lag on the old side, and knob moves
// read as a tape-style varispeed glide.
constexpr double kOffsetSmoothSeconds = 0.1;

// Below this the lag path blends back to the dry high band, so the center
// detent is exactly dual-mono and the tape-flange zone starts around 1 ms.
constexpr float kCenterBlendMs = 1.0f;

// Precedence patch: the earlier channel pulls the image toward itself, and a
// small bump on the late side takes most of that back.
constexpr float kLagGainDb = 1.5f;

// The 4-point Lagrange kernel needs one sample either side of the read
// position, so never ask for less than two samples of delay.
constexpr double kMinDelaySamples = 2.0;

constexpr double kDiffuserLoHz = 300.0;
constexpr double kDiffuserHiHz = 6000.0;

constexpr double kWobbleRateHz = 0.3;

int nextPowerOfTwo (int n)
{
    int p = 1;
    while (p < n)
        p <<= 1;
    return p;
}
} // namespace

// ----------------------------------------------------------------- Diffuser

void Spread::Diffuser::prepare (double sampleRate)
{
    for (int i = 0; i < numStages; ++i)
    {
        const double fc = kDiffuserLoHz
                        * std::pow (kDiffuserHiHz / kDiffuserLoHz, (double) i / (numStages - 1));

        // A corner above Nyquist puts tan() past pi/2 and the coefficient
        // outside |a| < 1: unstable. Pin just under; the top stages collapse
        // toward a = 0 (transparent) there.
        const double safe = std::min (fc, sampleRate * 0.49);
        const double t = std::tan (kPi * safe / sampleRate);
        stages[(size_t) i].a = (float) ((t - 1.0) / (t + 1.0));
    }
}

void Spread::Diffuser::reset()
{
    for (auto& stage : stages)
        stage.z = 0.0f;
}

// ---------------------------------------------------------------- Crossover

void Spread::Crossover::setCutoff (double frequencyHz, double sampleRate)
{
    const double fc = std::clamp (frequencyHz, 10.0, sampleRate * 0.45);
    const double k = std::tan (kPi * fc / sampleRate);
    const double kk = k * k;
    const double sqrt2 = std::sqrt (2.0);
    const double norm = 1.0 / (1.0 + sqrt2 * k + kk);

    a1 = (float) (2.0 * (kk - 1.0) * norm);
    a2 = (float) ((1.0 - sqrt2 * k + kk) * norm);

    lb0 = (float) (kk * norm);
    lb1 = 2.0f * lb0;
    lb2 = lb0;

    hb0 = (float) norm;
    hb1 = -2.0f * hb0;
    hb2 = hb0;
}

void Spread::Crossover::reset()
{
    lz1a[0] = lz1a[1] = lz2a[0] = lz2a[1] = 0.0f;
    lz1b[0] = lz1b[1] = lz2b[0] = lz2b[1] = 0.0f;
    hz1a[0] = hz1a[1] = hz2a[0] = hz2a[1] = 0.0f;
    hz1b[0] = hz1b[1] = hz2b[0] = hz2b[1] = 0.0f;
}

void Spread::Crossover::process (int channel, float x, float& low, float& high) noexcept
{
    // Each band is one 2nd-order Butterworth section run twice (Linkwitz-Riley
    // 4th order). Squaring the section is what makes low + high an allpass:
    // the two bands stay in phase at the corner and recombine flat.
    const auto biquad = [] (float in, float b0, float b1, float b2, float a1, float a2,
                            float& z1, float& z2) noexcept
    {
        const float y = b0 * in + z1;
        z1 = b1 * in - a1 * y + z2;
        z2 = b2 * in - a2 * y;
        return y;
    };

    const auto ch = (size_t) channel;

    // Two passes through the same section, each with its own state pair.
    float l = biquad (x, lb0, lb1, lb2, a1, a2, lz1a[ch], lz2a[ch]);
    l = biquad (l, lb0, lb1, lb2, a1, a2, lz1b[ch], lz2b[ch]);

    float h = biquad (x, hb0, hb1, hb2, a1, a2, hz1a[ch], hz2a[ch]);
    h = biquad (h, hb0, hb1, hb2, a1, a2, hz1b[ch], hz2b[ch]);

    low = l;
    high = h;
}

// ------------------------------------------------------------------- Wobble

void Spread::Wobble::prepare (double sampleRate)
{
    coeff = (float) (1.0 - std::exp (-2.0 * kPi * kWobbleRateHz / sampleRate));

    // Analytic normalisation: the two-pole shaper's impulse response is
    // h[n] = k^2 (n+1) a^n with a = 1-k, so for unit-variance input the
    // steady-state output variance is sum h^2 = k^4 (1+a^2) / (1-a^2)^3.
    // Uniform [-1, 1] noise has sigma^2 = 1/3; scale so 3 sigma reaches the
    // +-1 clamp. Only then does the depth knob span the full +-1.2 ms at any
    // sample rate.
    const double k = coeff, a = 1.0 - k;
    const double one = 1.0 - a * a;
    const double gainSq = k * k * k * k * (1.0 + a * a) / (one * one * one);
    const double sigma = std::sqrt (gainSq / 3.0);
    norm = sigma > 0.0 ? (float) (1.0 / (3.0 * sigma)) : 1.0f;
}

float Spread::Wobble::next() noexcept
{
    // xorshift32, uniform in [-1, 1]
    rngState ^= rngState << 13;
    rngState ^= rngState >> 17;
    rngState ^= rngState << 5;
    const float white = (float) ((double) rngState / 2147483648.0 - 1.0);

    state1 += coeff * (white - state1);
    state2 += coeff * (state1 - state2);
    return std::clamp (state2 * norm, -1.0f, 1.0f);
}

// --------------------------------------------------------------------- Ramp

void Spread::Ramp::reset (double sampleRate, double seconds)
{
    rampLength = std::max (1, (int) (sampleRate * seconds));
    steps = 0;
    step = 0.0f;
    current = target;
}

void Spread::Ramp::setTarget (float t) noexcept
{
    if (t == target)
        return;

    target = t;
    steps = rampLength;
    step = (target - current) / (float) rampLength;
}

float Spread::Ramp::next() noexcept
{
    if (steps <= 0)
        return current;

    current += step;
    if (--steps == 0)
        current = target;
    return current;
}

// ------------------------------------------------------------------- Spread

void Spread::prepare (double sampleRate, int maxBlockSize)
{
    (void) maxBlockSize;

    fs = sampleRate > 0.0 ? sampleRate : 48000.0;
    msToSamples = (float) (fs * 0.001);
    lagGain = std::pow (10.0f, kLagGainDb / 20.0f);

    const auto maxDelaySamples =
        (int) std::ceil ((double) (maxOffsetMs + maxWobbleMs) * 0.001 * fs) + 8;
    const auto size = nextPowerOfTwo (maxDelaySamples + 8);
    delayBuffer.assign ((size_t) size, 0.0f);
    bufferMask = size - 1;
    writePos = 0;

    crossover.setCutoff (appliedCrossoverHz, fs);
    diffuser.prepare (fs);
    wobble.prepare (fs);

    offsetCoeff = 1.0f - std::exp ((float) (-1.0 / (kOffsetSmoothSeconds * fs)));

    wetGain.reset (fs, kFadeSeconds);
    wobbleDepth.reset (fs, kFadeSeconds);
    crossoverMix.reset (fs, kFadeSeconds);
    diffuseMix.reset (fs, kFadeSeconds);

    running = false;
    engaged = false;
    reset();
}

void Spread::reset()
{
    std::fill (delayBuffer.begin(), delayBuffer.end(), 0.0f);
    writePos = 0;
    crossover.reset();
    diffuser.reset();
    wobble.reset();
}

void Spread::setParams (bool nowEngaged, const Params& params)
{
    if (! running)
    {
        if (! nowEngaged)
            return; // idle and staying idle

        // Engage from idle: clean deck, offset primed at the knob (no glide up
        // from a stale value; the wet fade-in covers the start), fade from dry.
        reset();
        offsetStateMs = params.offsetMs;
        wobbleDepth.snapTo (params.wobbleDepth);
        crossoverMix.snapTo (params.crossoverOn ? 1.0f : 0.0f);
        diffuseMix.snapTo (params.diffuseOn ? 1.0f : 0.0f);
        wetGain.snapTo (0.0f);
        running = true;
    }

    engaged = nowEngaged;
    targetOffsetMs = std::clamp (params.offsetMs, -maxOffsetMs, maxOffsetMs);
    wobbleDepth.setTarget (std::clamp (params.wobbleDepth, 0.0f, 1.0f));
    crossoverMix.setTarget (params.crossoverOn ? 1.0f : 0.0f);
    diffuseMix.setTarget (params.diffuseOn ? 1.0f : 0.0f);

    // Recompute the filter only when the knob actually moved: this is
    // transcendental math on the audio thread otherwise.
    const float wantedHz = std::clamp (params.crossoverHz, minCrossoverHz, maxCrossoverHz);
    if (wantedHz != appliedCrossoverHz)
    {
        appliedCrossoverHz = wantedHz;
        crossover.setCutoff (appliedCrossoverHz, fs);
    }

    wetGain.setTarget (engaged ? 1.0f : 0.0f);
}

float Spread::readDelayed (double delaySamples) const noexcept
{
    const double readPos = (double) writePos - delaySamples;
    const auto i = (int) std::floor (readPos);
    const auto f = (float) (readPos - (double) i);

    // 4-point Lagrange over buf[i-1], buf[i], buf[i+1], buf[i+2]. Linear
    // interpolation would low-pass in rhythm with the wobble.
    const float xm1 = delayBuffer[(size_t) ((i - 1) & bufferMask)];
    const float x0 = delayBuffer[(size_t) (i & bufferMask)];
    const float x1 = delayBuffer[(size_t) ((i + 1) & bufferMask)];
    const float x2 = delayBuffer[(size_t) ((i + 2) & bufferMask)];

    const float cm1 = -f * (f - 1.0f) * (f - 2.0f) * (1.0f / 6.0f);
    const float c0 = (f + 1.0f) * (f - 1.0f) * (f - 2.0f) * 0.5f;
    const float c1 = -(f + 1.0f) * f * (f - 2.0f) * 0.5f;
    const float c2 = (f + 1.0f) * f * (f - 1.0f) * (1.0f / 6.0f);

    return cm1 * xm1 + c0 * x0 + c1 * x1 + c2 * x2;
}

void Spread::process (float* left, float* right, int numSamples)
{
    if (! running)
        return;

    const float maxDelayMs = maxOffsetMs + maxWobbleMs;

    for (int i = 0; i < numSamples; ++i)
    {
        const float xl = left[i];
        const float xr = right[i];

        // Both channels are split, so both sides get the identical LR4 phase
        // rotation and each keeps its own lows (see the header).
        const float xoMix = crossoverMix.next();

        float lowL = 0.0f, highL = 0.0f, lowR = 0.0f, highR = 0.0f;
        crossover.process (0, xl, lowL, highL);
        crossover.process (1, xr, lowR, highR);

        // Blend each band toward the crossover's bypass (no low band, the
        // full signal in the high band). The filters keep running, so
        // re-engaging blends into warm state instead of ringing.
        lowL *= xoMix;
        lowR *= xoMix;
        highL = xl + (highL - xl) * xoMix;
        highR = xr + (highR - xr) * xoMix;

        // Smooth the SIGNED offset; the side and the magnitude both derive
        // from the result, so a sweep through zero passes through identity.
        offsetStateMs += offsetCoeff * (targetOffsetMs - offsetStateMs);
        const float tMs = std::abs (offsetStateMs);
        const bool lagOnRight = offsetStateMs >= 0.0f;

        const float srcLow = lagOnRight ? lowR : lowL;
        const float srcHigh = lagOnRight ? highR : highL;

        delayBuffer[(size_t) writePos] = srcHigh;

        const float wobbleMs = maxWobbleMs * wobbleDepth.next() * wobble.next();
        const float delayMs = std::clamp (tMs + wobbleMs, 0.0f, maxDelayMs);

        float lag = readDelayed (std::max (kMinDelaySamples, (double) (delayMs * msToSamples)));

        // Diffusion cascade, blended toward its bypass (the pure delay).
        lag += (diffuser.process (lag) - lag) * diffuseMix.next();
        lag *= lagGain;

        // Center-identity blend: below kCenterBlendMs the lag path converges
        // to the dry high band, so the detent is exactly dual-mono.
        const float blend = std::min (tMs * (1.0f / kCenterBlendMs), 1.0f);
        lag = srcHigh + (lag - srcHigh) * blend;

        // The reference channel is its own bands recombined: LR4 low + high is
        // allpass-flat, so it matches the lag channel's rotation exactly.
        const float refOut = lagOnRight ? (lowL + highL) : (lowR + highR);
        const float lagOut = srcLow + lag;

        const float wet = wetGain.next();
        left[i] = xl + ((lagOnRight ? refOut : lagOut) - xl) * wet;
        right[i] = xr + ((lagOnRight ? lagOut : refOut) - xr) * wet;

        writePos = (writePos + 1) & bufferMask;
    }

    // Disengage completes once the fade-out lands: output equals input again.
    if (! engaged && ! wetGain.isRamping() && wetGain.value() <= 0.0f)
        running = false;
}

} // namespace nsdsp
