#pragma once

#include <cstdint>
#include <vector>

namespace nsdsp
{

// End-of-chain "doubler" in the style of the ones found in Neural DSP amp
// suites: two artificial takes of the input, micro-pitch-shifted in opposite
// directions (delay-line pitch shifter with dual crossfaded taps), delayed by
// a short, slowly drifting time, panned hard left / hard right, and blended
// with the dry signal. The "humanize" control adds slow random drift to the
// delay times, mimicking the timing sloppiness of a real doubled take.
//
// JUCE-free so that it can be shared with the plain LV2 (MOD) build.
class Doubler
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (bool enabled, float timeMs, float detuneCents, float humanize, float width, float mix);

    // Stereo in-place processing.
    void process (float* left, float* right, int numSamples);

private:
    struct Voice
    {
        float detuneSign = 1.0f;   // +1 pitch up, -1 pitch down
        float delayScale = 1.0f;   // relative offset between the two takes
        double phase = 0.0;        // pitch-shifter tap phase, in samples [0, window)

        // slow random drift (humanize)
        float drift = 0.0f;
        float driftTarget = 0.0f;
        int driftCountdown = 0;

        // gentle high-cut on the wet takes
        float lpState = 0.0f;

        float panLeft = 1.0f, panRight = 0.0f;
    };

    // minimal one-pole parameter smoother
    struct Smoother
    {
        void reset (double sampleRate, double timeSeconds);
        void setTarget (float t) noexcept { target = t; }
        float next() noexcept { return current += coeff * (target - current); }
        float current = 0.0f, target = 0.0f, coeff = 1.0f;
    };

    float readInterpolated (double delaySamples) const;
    void updateVoiceDrift (Voice& voice);
    float nextRandom() noexcept; // uniform in [-1, 1]

    double fs = 48000.0;
    std::vector<float> buffer;
    int bufferMask = 0;
    int writePos = 0;
    int windowSamples = 1;
    float lpCoeff = 0.5f;

    Voice voices[2];
    uint32_t rngState = 0x12345678u;

    bool enabled = false;
    float timeMs = 18.0f, detuneCents = 9.0f, humanize = 0.3f, width = 1.0f, mix = 0.5f;
    Smoother dryGain, wetGain;
};

} // namespace nsdsp
