#include "Doubler.h"

#include <cmath>

namespace nsdsp
{

namespace
{
constexpr float maxTimeMs = 60.0f;
constexpr float windowMs = 40.0f;   // pitch-shifter grain window
constexpr float maxDriftMs = 6.0f;  // humanize drift range
constexpr float wetToneHz = 11000.0f;
} // namespace

void Doubler::prepare (double sampleRate, int maxBlockSize)
{
    juce::ignoreUnused (maxBlockSize);
    fs = sampleRate;

    const auto maxDelaySamples = (int) std::ceil (fs * 0.001 * (maxTimeMs * 1.3 + windowMs + maxDriftMs + 10.0));
    const auto size = juce::nextPowerOfTwo (maxDelaySamples + 8);
    buffer.assign ((size_t) size, 0.0f);
    bufferMask = size - 1;
    writePos = 0;

    windowSamples = juce::jmax (16, (int) std::round (fs * 0.001 * windowMs));
    lpCoeff = 1.0f - std::exp (-2.0f * juce::MathConstants<float>::pi * wetToneHz / (float) fs);
    lpCoeff = juce::jlimit (0.0f, 1.0f, lpCoeff);

    voices[0] = {};
    voices[0].detuneSign = 1.0f;
    voices[0].delayScale = 1.0f;
    voices[1] = {};
    voices[1].detuneSign = -1.0f;
    voices[1].delayScale = 1.22f;
    voices[1].phase = windowSamples * 0.5;

    dryGain.reset (fs, 0.03);
    wetGain.reset (fs, 0.03);
    reset();
}

void Doubler::reset()
{
    std::fill (buffer.begin(), buffer.end(), 0.0f);
    for (auto& voice : voices)
    {
        voice.lpState = 0.0f;
        voice.drift = 0.0f;
        voice.driftTarget = 0.0f;
        voice.driftCountdown = 0;
    }
}

void Doubler::setParams (bool isEnabled, float newTimeMs, float newDetuneCents,
                         float newHumanize, float newWidth, float newMix)
{
    enabled = isEnabled;
    timeMs = juce::jlimit (2.0f, maxTimeMs, newTimeMs);
    detuneCents = juce::jlimit (0.0f, 30.0f, newDetuneCents);
    humanize = juce::jlimit (0.0f, 1.0f, newHumanize);
    width = juce::jlimit (0.0f, 1.0f, newWidth);
    mix = juce::jlimit (0.0f, 1.0f, newMix);

    // Constant-power dry/wet blend.
    const auto mixAngle = mix * juce::MathConstants<float>::halfPi;
    dryGain.setTargetValue (std::cos (mixAngle));
    wetGain.setTargetValue (std::sin (mixAngle));

    // Voice panning: spread the two takes out to the sides as width grows.
    for (int v = 0; v < 2; ++v)
    {
        const auto pan = (v == 0 ? -1.0f : 1.0f) * width;
        const auto angle = (pan + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
        voices[v].panLeft = std::cos (angle);
        voices[v].panRight = std::sin (angle);
    }
}

float Doubler::readInterpolated (double delaySamples) const
{
    const double readPos = (double) writePos - delaySamples;
    const auto floorPos = (int) std::floor (readPos);
    const auto frac = (float) (readPos - (double) floorPos);

    const auto i0 = floorPos & bufferMask;
    const auto i1 = (floorPos + 1) & bufferMask;
    return buffer[(size_t) i0] + frac * (buffer[(size_t) i1] - buffer[(size_t) i0]);
}

void Doubler::updateVoiceDrift (Voice& voice)
{
    if (--voice.driftCountdown <= 0)
    {
        voice.driftTarget = random.nextFloat() * 2.0f - 1.0f;
        voice.driftCountdown = (int) (fs * (0.3 + 0.9 * random.nextDouble()));
    }

    // One-pole glide towards the target (~200 ms time constant).
    const auto glide = 1.0f - std::exp (-1.0f / (0.2f * (float) fs));
    voice.drift += glide * (voice.driftTarget - voice.drift);
}

void Doubler::process (juce::AudioBuffer<float>& stereo, int numSamples)
{
    if (! enabled)
        return;

    auto* left = stereo.getWritePointer (0);
    auto* right = stereo.getWritePointer (1);

    const auto window = (double) windowSamples;

    for (int i = 0; i < numSamples; ++i)
    {
        const auto dryL = left[i];
        const auto dryR = right[i];

        buffer[(size_t) writePos] = 0.5f * (dryL + dryR);

        float wetL = 0.0f, wetR = 0.0f;

        for (auto& voice : voices)
        {
            updateVoiceDrift (voice);

            const auto cents = voice.detuneSign * detuneCents * (1.0f + 0.35f * humanize * voice.drift);
            const auto ratio = std::pow (2.0, (double) cents / 1200.0);

            // The tap phase drifts so that the read head moves at `ratio`
            // times real time, producing the micro pitch shift.
            voice.phase += (1.0 - ratio);
            while (voice.phase >= window) voice.phase -= window;
            while (voice.phase < 0.0)    voice.phase += window;

            const auto baseDelay = (double) fs * 0.001
                                 * ((double) timeMs * (double) voice.delayScale
                                    + (double) (maxDriftMs * humanize) * (double) voice.drift);

            // Dual taps half a window apart, crossfaded so the wrap point of
            // each tap falls where its gain is zero.
            auto phase2 = voice.phase + window * 0.5;
            if (phase2 >= window) phase2 -= window;

            const auto g1 = std::sin (juce::MathConstants<float>::pi * (float) (voice.phase / window));
            const auto g2 = std::sin (juce::MathConstants<float>::pi * (float) (phase2 / window));

            auto sample = g1 * readInterpolated (juce::jmax (1.0, baseDelay + voice.phase))
                        + g2 * readInterpolated (juce::jmax (1.0, baseDelay + phase2));

            // Soften the top end of the fake take.
            voice.lpState += lpCoeff * (sample - voice.lpState);
            sample = voice.lpState;

            wetL += sample * voice.panLeft;
            wetR += sample * voice.panRight;
        }

        const auto dg = dryGain.getNextValue();
        const auto wg = wetGain.getNextValue();

        left[i] = dryL * dg + wetL * wg;
        right[i] = dryR * dg + wetR * wg;

        writePos = (writePos + 1) & bufferMask;
    }
}

} // namespace nsdsp
