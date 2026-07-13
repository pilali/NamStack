#include "IRMixer.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nsdsp
{

namespace
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSqrt2 = 1.41421356237309504880f;
} // namespace

void IRMixer::prepare (double sampleRate, int partitionSize, int maxBlockSize)
{
    fs = sampleRate;
    P = partitionSize;

    // ~20 ms gain smoothing, applied once per sample inside the partition mix
    smoothCoeff = (float) (1.0 - std::exp (-1.0 / (0.02 * fs)));

    seg.assign ((size_t) P, 0.0f);
    tmpL.assign ((size_t) P, 0.0f);
    tmpR.assign ((size_t) P, 0.0f);
    mixL.assign ((size_t) P, 0.0f);
    mixR.assign ((size_t) P, 0.0f);

    fifoSize = 1;
    while (fifoSize < 2 * P + std::max (P, maxBlockSize))
        fifoSize <<= 1;
    fifoL.assign ((size_t) fifoSize, 0.0f);
    fifoR.assign ((size_t) fifoSize, 0.0f);

    reset();
}

void IRMixer::reset()
{
    segFill = 0;
    fifoRead = fifoWrite = fifoCount = 0;
    latency = 0;
    std::fill (seg.begin(), seg.end(), 0.0f);

    for (auto& slot : slots)
    {
        if (slot.convolver != nullptr)
            slot.convolver->reset();
        slot.gainL = slot.targetGainL;
        slot.gainR = slot.targetGainR;
    }
}

void IRMixer::setSlotParams (int slot, bool enabled, float gainDb, float pan)
{
    if (slot < 0 || slot >= numSlots)
        return;

    auto& s = slots[(size_t) slot];
    s.enabled = enabled;

    const auto gain = std::pow (10.0f, gainDb * 0.05f);
    const auto angle = (std::clamp (pan, -1.0f, 1.0f) + 1.0f) * kPi * 0.25f;
    s.targetGainL = gain * std::cos (angle) * kSqrt2;
    s.targetGainR = gain * std::sin (angle) * kSqrt2;
}

Convolver* IRMixer::exchangeConvolver (int slot, Convolver* newConvolver)
{
    if (slot < 0 || slot >= numSlots)
        return newConvolver;

    auto* old = slots[(size_t) slot].convolver;
    slots[(size_t) slot].convolver = newConvolver;
    return old;
}

void IRMixer::processPartition()
{
    bool anyActive = false;

    std::memset (mixL.data(), 0, (size_t) P * sizeof (float));
    std::memset (mixR.data(), 0, (size_t) P * sizeof (float));

    for (auto& slot : slots)
    {
        if (! slot.enabled || slot.convolver == nullptr)
            continue;

        anyActive = true;
        slot.convolver->processPartition (seg.data(), tmpL.data(), tmpR.data());

        for (int i = 0; i < P; ++i)
        {
            slot.gainL += smoothCoeff * (slot.targetGainL - slot.gainL);
            slot.gainR += smoothCoeff * (slot.targetGainR - slot.gainR);
            mixL[(size_t) i] += tmpL[(size_t) i] * slot.gainL;
            mixR[(size_t) i] += tmpR[(size_t) i] * slot.gainR;
        }
    }

    if (! anyActive)
    {
        std::memcpy (mixL.data(), seg.data(), (size_t) P * sizeof (float));
        std::memcpy (mixR.data(), seg.data(), (size_t) P * sizeof (float));
    }

    // push into the output fifo
    for (int i = 0; i < P; ++i)
    {
        fifoL[(size_t) fifoWrite] = mixL[(size_t) i];
        fifoR[(size_t) fifoWrite] = mixR[(size_t) i];
        fifoWrite = (fifoWrite + 1) & (fifoSize - 1);
    }
    fifoCount += P;
}

void IRMixer::process (const float* monoIn, float* outL, float* outR, int numSamples)
{
    for (int i = 0; i < numSamples; ++i)
    {
        seg[(size_t) segFill++] = monoIn[i];
        if (segFill == P)
        {
            processPartition();
            segFill = 0;
        }
    }

    // When the host block size is a multiple of the partition size (MOD:
    // block == partition) the fifo now holds exactly numSamples and no
    // latency is added. Otherwise the shortfall is padded with zeros once,
    // which settles into a constant latency of up to one partition.
    int toEmit = numSamples;
    if (fifoCount < numSamples)
    {
        const auto shortfall = numSamples - fifoCount;
        latency = std::max (latency, shortfall);
        std::memset (outL, 0, (size_t) shortfall * sizeof (float));
        std::memset (outR, 0, (size_t) shortfall * sizeof (float));
        outL += shortfall;
        outR += shortfall;
        toEmit -= shortfall;
    }

    for (int i = 0; i < toEmit; ++i)
    {
        outL[i] = fifoL[(size_t) fifoRead];
        outR[i] = fifoR[(size_t) fifoRead];
        fifoRead = (fifoRead + 1) & (fifoSize - 1);
    }
    fifoCount -= toEmit;
}

} // namespace nsdsp
