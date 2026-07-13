#pragma once

#include "Convolver.h"

#include <memory>
#include <vector>

namespace nsdsp
{

// Four parallel IR convolution slots mixed to stereo with per-slot level and
// constant-power pan. Input is segmented into fixed-size partitions for the
// convolvers; when the host block size equals the partition size (the normal
// case on MOD devices) no extra latency is introduced, otherwise up to one
// partition of latency appears and is reported through getLatencySamples().
class IRMixer
{
public:
    static constexpr int numSlots = 4;

    void prepare (double sampleRate, int partitionSize, int maxBlockSize);
    void reset();

    // Audio-thread setters (values are picked up at the next partition).
    void setSlotParams (int slot, bool enabled, float gainDb, float pan);

    // Swap in a convolver built on a worker thread (nullptr clears the slot).
    // Returns the previous convolver, to be freed off the audio thread.
    Convolver* exchangeConvolver (int slot, Convolver* newConvolver);
    bool slotHasIR (int slot) const { return slots[(size_t) slot].convolver != nullptr; }

    // monoIn -> stereo out; outL/outR may not alias monoIn.
    void process (const float* monoIn, float* outL, float* outR, int numSamples);

    int getPartitionSize() const noexcept { return P; }
    int getLatencySamples() const noexcept { return latency; }

private:
    void processPartition();

    struct Slot
    {
        Convolver* convolver = nullptr; // owned; freed through exchangeConvolver()
        bool enabled = false;
        float targetGainL = 0.0f, targetGainR = 0.0f;
        float gainL = 0.0f, gainR = 0.0f;
    };

    Slot slots[numSlots];

    double fs = 48000.0;
    int P = 128;
    float smoothCoeff = 0.01f;

    std::vector<float> seg;        // partition input accumulator
    int segFill = 0;
    std::vector<float> tmpL, tmpR; // per-slot convolution output
    std::vector<float> mixL, mixR; // partition mix bus

    std::vector<float> fifoL, fifoR; // output ring
    int fifoRead = 0, fifoWrite = 0, fifoCount = 0, fifoSize = 0;

    int latency = 0;
};

} // namespace nsdsp
