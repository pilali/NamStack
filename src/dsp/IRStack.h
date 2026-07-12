#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>

#include <array>
#include <atomic>

namespace nsdsp
{

// Up to four impulse responses convolved in parallel, each with its own
// level and stereo pan, mixed to a stereo bus.
class IRStack
{
public:
    static constexpr int numSlots = 4;

    void prepare (const juce::dsp::ProcessSpec& monoInputSpec);
    void reset();

    // Thread-safe: juce::dsp::Convolution swaps the IR on a background thread.
    void loadIR (int slot, const juce::File& file);
    void clearIR (int slot);
    bool isLoaded (int slot) const { return slots[(size_t) slot].loaded.load(); }
    juce::String getIRName (int slot) const;

    void setSlotParams (int slot, bool enabled, float gainDb, float pan);

    // Convolves the mono input through every active slot and mixes the result
    // into stereoOut (channels 0 and 1). When no slot is active the dry mono
    // signal is copied to both output channels.
    void process (const float* monoIn, juce::AudioBuffer<float>& stereoOut, int numSamples);

private:
    struct Slot
    {
        juce::dsp::Convolution convolution { juce::dsp::Convolution::Latency { 0 } };
        std::atomic<bool> loaded { false };
        std::atomic<bool> enabled { false };
        juce::SmoothedValue<float> gainLeft, gainRight;
        std::atomic<float> targetGainDb { 0.0f };
        std::atomic<float> targetPan { 0.0f };
        juce::String name;
    };

    std::array<Slot, numSlots> slots;
    juce::AudioBuffer<float> slotBuffer;
    double sampleRate = 48000.0;
    juce::CriticalSection nameLock;
};

} // namespace nsdsp
