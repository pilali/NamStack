#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
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
    // How long a requested IR is given to become the live one.
    // loadImpulseResponse() is asynchronous -- it posts to
    // juce::dsp::Convolution's own background thread, which then reads, trims,
    // resamples and transforms the file -- so for a while after the call the
    // *previous* IR is still what is being heard. The mono decision therefore
    // follows the live IR, not the requested one: it is held for this long and
    // only then adopted, in both directions.
    //
    // Both directions matter. Adopting "stereo" early would render the still
    // live mono IR through two engines, which is merely wasteful; adopting it
    // late is what avoids the real hazard. While the shortcut is on, the
    // channel-1 engine is not fed, so its frequency-delay line freezes on
    // whatever audio it last saw -- possibly minutes old. Feeding that engine
    // again while it is still the live one would convolve those stale spectra
    // out over a full IR length: an audible ghost. Waiting means the switch
    // back to two channels always lands on a freshly built engine instead.
    static constexpr double settleSeconds = 1.0;

    struct Slot
    {
        juce::dsp::Convolution convolution { juce::dsp::Convolution::Latency { 0 } };
        std::atomic<bool> loaded { false };
        std::atomic<bool> enabled { false };

        // A mono IR makes the two convolution engines compute the same thing:
        // JUCE's MultichannelEngine always builds two of them (its numChannels
        // is a hard-coded 2) and hands a mono IR to both, while this class
        // feeds the same mono signal to both inputs. So the slot convolves one
        // channel and duplicates it, halving the FFT work.
        //
        // `pending` is what the last requested file asks for (message thread);
        // `live` is what the audio thread acts on, adopted from `pending` when
        // the settling window runs out. Only the audio thread touches `live`.
        std::atomic<bool> irIsMonoPending { false };
        bool irIsMonoLive = false;

        // Samples left of the settling window (see settleSeconds).
        std::atomic<int> monoSettle { 0 };
        juce::SmoothedValue<float> gainLeft, gainRight;
        std::atomic<float> targetGainDb { 0.0f };
        std::atomic<float> targetPan { 0.0f };
        juce::String name;
    };

    std::array<Slot, numSlots> slots;
    juce::AudioBuffer<float> slotBuffer;
    double sampleRate = 48000.0;
    juce::CriticalSection nameLock;

    // Message thread only: reads the header of a picked IR to learn its
    // channel count, which juce::dsp::Convolution never exposes.
    juce::AudioFormatManager formatManager;
};

} // namespace nsdsp
