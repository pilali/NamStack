#include "Convolver.h"

#include <pffft.h>

#include <algorithm>
#include <cstring>

namespace nsdsp
{

namespace
{
bool isPowerOfTwo (int n)
{
    return n > 0 && (n & (n - 1)) == 0;
}
} // namespace

Convolver::~Convolver()
{
    release();
}

void Convolver::release()
{
    if (setup != nullptr)
    {
        pffft_destroy_setup (setup);
        setup = nullptr;
    }

    for (auto** p : { &slide, &fdl, &ir[0], &ir[1], &acc, &work, &td })
    {
        if (*p != nullptr)
        {
            pffft_aligned_free (*p);
            *p = nullptr;
        }
    }
}

bool Convolver::init (const float* const* irChannels, int numChannels, int irLength, int partitionSize)
{
    release();

    if (irChannels == nullptr || numChannels < 1 || irLength < 1
        || ! isPowerOfTwo (partitionSize) || partitionSize < 16)
        return false;

    P = partitionSize;
    F = 2 * P;
    K = (irLength + P - 1) / P;
    numCh = std::min (numChannels, 2);

    setup = pffft_new_setup (F, PFFFT_REAL);
    if (setup == nullptr)
    {
        release();
        return false;
    }

    const auto alloc = [] (size_t numFloats) {
        auto* p = static_cast<float*> (pffft_aligned_malloc (numFloats * sizeof (float)));
        if (p != nullptr)
            std::memset (p, 0, numFloats * sizeof (float));
        return p;
    };

    slide = alloc ((size_t) F);
    fdl = alloc ((size_t) K * (size_t) F);
    acc = alloc ((size_t) F);
    work = alloc ((size_t) F);
    td = alloc ((size_t) F);

    bool ok = slide && fdl && acc && work && td;

    for (int ch = 0; ok && ch < numCh; ++ch)
    {
        ir[ch] = alloc ((size_t) K * (size_t) F);
        ok = ir[ch] != nullptr;

        for (int k = 0; ok && k < K; ++k)
        {
            // time-domain partition: P samples of IR, zero-padded to F
            std::memset (td, 0, (size_t) F * sizeof (float));
            const auto remaining = std::min (P, irLength - k * P);
            std::memcpy (td, irChannels[ch] + k * P, (size_t) remaining * sizeof (float));

            pffft_transform (setup, td, ir[ch] + (size_t) k * (size_t) F, work, PFFFT_FORWARD);
        }
    }

    if (! ok)
    {
        release();
        return false;
    }

    fdlPos = 0;
    return true;
}

void Convolver::reset()
{
    if (setup == nullptr)
        return;

    std::memset (slide, 0, (size_t) F * sizeof (float));
    std::memset (fdl, 0, (size_t) K * (size_t) F * sizeof (float));
    fdlPos = 0;
}

void Convolver::processPartition (const float* in, float* outL, float* outR)
{
    if (setup == nullptr)
    {
        // pass-through (should not happen: unloaded slots are skipped)
        std::memcpy (outL, in, (size_t) P * sizeof (float));
        std::memcpy (outR, in, (size_t) P * sizeof (float));
        return;
    }

    // slide the input window: [previous P | new P]
    std::memmove (slide, slide + P, (size_t) P * sizeof (float));
    std::memcpy (slide + P, in, (size_t) P * sizeof (float));

    pffft_transform (setup, slide, fdl + (size_t) fdlPos * (size_t) F, work, PFFFT_FORWARD);

    const float scale = 1.0f / (float) F;

    for (int ch = 0; ch < numCh; ++ch)
    {
        std::memset (acc, 0, (size_t) F * sizeof (float));

        for (int k = 0; k < K; ++k)
        {
            const auto idx = (fdlPos - k + K) % K;
            pffft_zconvolve_accumulate (setup,
                                        fdl + (size_t) idx * (size_t) F,
                                        ir[ch] + (size_t) k * (size_t) F,
                                        acc, 1.0f);
        }

        pffft_transform (setup, acc, td, work, PFFFT_BACKWARD);

        // overlap-save: the last P samples are the valid ones
        auto* out = (ch == 0) ? outL : outR;
        for (int i = 0; i < P; ++i)
            out[i] = td[P + i] * scale;
    }

    if (numCh == 1)
        std::memcpy (outR, outL, (size_t) P * sizeof (float));

    fdlPos = (fdlPos + 1) % K;
}

} // namespace nsdsp
