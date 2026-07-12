#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace nsdsp
{

// End-of-chain "doubler" in the style of the ones found in Neural DSP amp
// suites: two artificial takes of the input, micro-pitch-shifted in opposite
// directions (delay-line pitch shifter with dual crossfaded taps), delayed by
// a short, slowly drifting time, panned hard left / hard right, and blended
// with the dry signal. The "humanize" control adds slow random drift to the
// delay times, mimicking the timing sloppiness of a real doubled take.
class Doubler
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void setParams (bool enabled, float timeMs, float detuneCents, float humanize, float width, float mix);

    void process (juce::AudioBuffer<float>& stereo, int numSamples);

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

    float readInterpolated (double delaySamples) const;
    void updateVoiceDrift (Voice& voice);

    double fs = 48000.0;
    std::vector<float> buffer;
    int bufferMask = 0;
    int writePos = 0;
    int windowSamples = 1;
    float lpCoeff = 0.5f;

    Voice voices[2];
    juce::Random random;

    bool enabled = false;
    float timeMs = 18.0f, detuneCents = 9.0f, humanize = 0.3f, width = 1.0f, mix = 0.5f;
    juce::SmoothedValue<float> dryGain, wetGain;
};

} // namespace nsdsp
