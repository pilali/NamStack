#pragma once

#include <string>
#include <vector>

namespace nsdsp
{

// Loads a WAV impulse response (via dr_wav), resamples it to the host rate
// (windowed-sinc) and normalises its energy. JUCE-free, for the plain LV2
// (MOD) build. Runs on a worker thread.
struct IRData
{
    std::vector<float> channels[2];
    int numChannels = 0;
    int length = 0;

    bool isValid() const noexcept { return numChannels > 0 && length > 0; }
};

// maxSamples caps the (post-resampling) IR length to bound the CPU cost on
// embedded targets.
bool loadIRFile (const std::string& path, double targetSampleRate, int maxSamples,
                 IRData& out, std::string& errorMessage);

} // namespace nsdsp
