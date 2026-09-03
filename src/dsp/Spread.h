#pragma once

#include <array>
#include <cstdint>
#include <vector>

namespace nsdsp
{

// End-of-chain stereo image built the way an engineer does it with automatic
// double tracking (ADT): one side gets the performance, the other a slightly
// late, subtly drifting copy. The drift is the whole trick -- a static delay
// reads as one guitar through a comb filter, a delay that wanders by a few
// tenths of a millisecond reads as a second take.
//
//   chain out (L, R)
//        |
//        +-- LR4 crossover per channel (default 130 Hz, 32.5-520 Hz, off-able)
//        |         |
//        |     low band  ------------------> stays on its own channel
//        |         |
//        |     high band --+---------------> reference channel, untouched
//        |                 |
//        |                 +--> lag deck --> lagged channel
//        |
//   lag deck = wobbling fractional delay (4-point Lagrange)
//            + 6-stage allpass diffusion cascade (300 Hz - 6 kHz, off-able)
//            + 1.5 dB precedence trim
//
// This replaces the earlier micro-pitch-shift doubler, whose two crossfaded
// grain taps left an audible flutter on sustained notes and whose dry/wet mix
// let the two takes comb against a third, undelayed copy of themselves. The
// design here is the one used by the TONE3000 plugin's Spread (see
// plugin/include/Spread.h and plugin/docs/stereo-image.md in that project),
// reimplemented without JUCE so the MOD LV2 build can share it.
//
// Design notes:
//   * Crossover, default 130 Hz. The low band never reaches the delay, so the
//     low E string and everything under it stays where it was: mono
//     compatibility problems live in the lows, and this makes them impossible
//     by construction. Switch the section off for full-band doubling --
//     maximum width, mono safety traded away knowingly.
//   * Both channels run their own crossover, unlike the TONE3000 original,
//     which seeds a single deck from channel 0 because its chain is mono
//     there. NamStack's IR mixer can pan four slots, so the source really is
//     stereo: splitting both channels keeps each one's own lows and gives
//     both sides the identical LR4 phase rotation, which is what makes the
//     low band fold down cleanly. With a mono source the two paths are
//     identical and the result matches the original sample for sample.
//   * The delay wobbles on a random walk (white noise through two cascaded
//     0.3 Hz one-poles). One pole is not enough: its 6 dB/oct tail leaves
//     noise variance above 20 Hz, and audio-rate delay-time noise FMs the
//     delayed channel into broadband fizz. Interpolation is 4-point Lagrange
//     because linear interpolation under a time-varying fractional offset
//     low-passes in rhythm with the wobble.
//   * Six first-order allpasses with fixed, log-spaced corners decorrelate
//     phase without touching magnitude (the allpass decorrelators of O. Das,
//     "An Open-Source Stereo Widening Plugin", DAFx24). Static coefficients
//     on purpose: movement comes from the delay, and modulating an allpass
//     coefficient would put the phasiness back.
//   * +1.5 dB on the lag side patches most of the precedence pull toward the
//     early channel. A patch, not a cure -- which is also why the offset's
//     sign "points at the fake one".
//
// There is no dry/wet mix: while spread is on the deck runs at full strength
// and Offset is the only musical dimension. Engage/bypass is a ~25 ms
// crossfade between the untouched input and the doubled image, and the
// section switches are ~25 ms blends too (both endpoints are magnitude-flat
// but differ in phase, so an instant switch would step the waveform).
//
// JUCE-free so that it can be shared with the plain LV2 (MOD) build. Audio
// thread only, zero allocation after prepare().
class Spread
{
public:
    static constexpr float maxOffsetMs = 24.0f;
    static constexpr float maxWobbleMs = 1.2f;   // 100% depth, absolute
    static constexpr float minCrossoverHz = 32.5f;
    static constexpr float maxCrossoverHz = 520.0f;
    static constexpr float defaultCrossoverHz = 130.0f;

    struct Params
    {
        // Signed: the sign picks the lagged channel (> 0 lags the right),
        // the magnitude the base delay.
        float offsetMs = 15.0f;
        float wobbleDepth = 0.25f; // 0..1 of the +-1.2 ms range; 0 when off
        float crossoverHz = defaultCrossoverHz;
        bool crossoverOn = true;   // off: the whole band is doubled
        bool diffuseOn = true;     // off: the lag side is a pure delay
    };

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    // engaged is the power switch: turning it off starts the fade-out
    // (isRunning() stays true until it lands), turning it on from idle resets
    // the deck and fades in from the untouched input.
    void setParams (bool engaged, const Params& params);

    // True while the engine still needs process() this block; false = fully
    // idle and safe to skip.
    bool isRunning() const { return running; }

    // Stereo in-place processing.
    void process (float* left, float* right, int numSamples);

private:
    // First-order allpass, transposed direct form II. Static coefficient.
    struct Allpass
    {
        float a = 0.0f, z = 0.0f;
        float process (float x) noexcept
        {
            const float v = x - a * z;
            const float y = a * v + z;
            z = v;
            return y;
        }
    };

    // Six-stage phase-diffusion cascade, corners log-spaced 300 Hz - 6 kHz.
    struct Diffuser
    {
        static constexpr int numStages = 6;
        void prepare (double sampleRate);
        void reset();
        float process (float x) noexcept
        {
            for (auto& stage : stages)
                x = stage.process (x);
            return x;
        }
        std::array<Allpass, numStages> stages;
    };

    // Linkwitz-Riley 4th order: two cascaded 2nd-order Butterworth sections
    // per band. The low and high outputs sum to an allpass, so recombining
    // them is magnitude-flat -- which is what lets the bypass blend and the
    // reference channel stay level.
    struct Crossover
    {
        void setCutoff (double frequencyHz, double sampleRate);
        void reset();
        // channel: 0 or 1; the two share coefficients, not state.
        void process (int channel, float x, float& low, float& high) noexcept;

        // Shared denominator, one numerator per band. Each band runs its
        // section twice, so it carries two state pairs per channel (a/b).
        float a1 = 0.0f, a2 = 0.0f;
        float lb0 = 0.0f, lb1 = 0.0f, lb2 = 0.0f;
        float hb0 = 0.0f, hb1 = 0.0f, hb2 = 0.0f;
        float lz1a[2] { 0.0f, 0.0f }, lz2a[2] { 0.0f, 0.0f };
        float lz1b[2] { 0.0f, 0.0f }, lz2b[2] { 0.0f, 0.0f };
        float hz1a[2] { 0.0f, 0.0f }, hz2a[2] { 0.0f, 0.0f };
        float hz1b[2] { 0.0f, 0.0f }, hz2b[2] { 0.0f, 0.0f };
    };

    // Random walk: uniform noise through two cascaded 0.3 Hz one-poles,
    // normalised so 3 sigma reaches the +-1 clamp at any sample rate.
    struct Wobble
    {
        void prepare (double sampleRate);
        void reset() { state1 = state2 = 0.0f; }
        float next() noexcept;

        uint32_t rngState = 0x9e3779b9u;
        float state1 = 0.0f, state2 = 0.0f;
        float coeff = 0.0f, norm = 1.0f;
    };

    // Linear ramp towards a target, used for every blend in the engine.
    struct Ramp
    {
        void reset (double sampleRate, double seconds);
        void setTarget (float t) noexcept;
        void snapTo (float v) noexcept { current = target = v; steps = 0; }
        float next() noexcept;
        bool isRamping() const noexcept { return steps > 0; }
        float value() const noexcept { return current; }

        float current = 0.0f, target = 0.0f, step = 0.0f;
        int steps = 0, rampLength = 1;
    };

    float readDelayed (double delaySamples) const noexcept;

    double fs = 48000.0;
    float msToSamples = 48.0f;

    std::vector<float> delayBuffer;
    int bufferMask = 0;
    int writePos = 0;

    Crossover crossover;
    Diffuser diffuser;
    Wobble wobble;

    Ramp wetGain, wobbleDepth, crossoverMix, diffuseMix;

    bool running = false;
    bool engaged = false;

    float targetOffsetMs = 0.0f;
    float offsetStateMs = 0.0f; // smoothed SIGNED offset (sign = lagged side)
    float offsetCoeff = 0.0f;

    float appliedCrossoverHz = defaultCrossoverHz;
    float lagGain = 1.0f;
};

} // namespace nsdsp
