#pragma once

#include <cstddef>

struct PFFFT_Setup;

namespace nsdsp
{

// Uniformly partitioned overlap-save FFT convolution (mono in, up to stereo
// out), built on pffft. One instance per IR slot; instances are immutable
// after init() so they can be built on a worker thread and swapped into the
// audio thread atomically.
class Convolver
{
public:
    Convolver() = default;
    ~Convolver();

    Convolver (const Convolver&) = delete;
    Convolver& operator= (const Convolver&) = delete;

    // irChannels: numChannels pointers to irLength samples, already at the
    // host sample rate. partitionSize must be a power of two >= 16.
    bool init (const float* const* irChannels, int numChannels, int irLength, int partitionSize);

    void reset();

    int getNumChannels() const noexcept { return numCh; }
    int getPartitionSize() const noexcept { return P; }

    // Processes exactly one partition: P input samples -> P samples per
    // output channel. For mono IRs both outputs receive the same signal.
    void processPartition (const float* in, float* outL, float* outR);

private:
    void release();

    PFFFT_Setup* setup = nullptr;
    int P = 0, F = 0, K = 0, numCh = 0;
    float* slide = nullptr; // 2P time-domain sliding input
    float* fdl = nullptr;   // K*F input spectra ring (pffft internal layout)
    float* ir[2] = {};      // K*F IR spectra per channel
    float* acc = nullptr;   // F accumulator
    float* work = nullptr;  // F pffft scratch
    float* td = nullptr;    // F time-domain result
    int fdlPos = 0;
};

} // namespace nsdsp
