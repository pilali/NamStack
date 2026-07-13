#include "IRLoader.h"

#define DR_WAV_IMPLEMENTATION
#include <dr_wav.h>

#include <algorithm>
#include <cmath>
#include <cstring>

namespace nsdsp
{

namespace
{
// Windowed-sinc offline resampler (Hann window, radius 16). Quality is more
// than sufficient for cabinet IRs.
std::vector<float> resample (const std::vector<float>& in, double ratio)
{
    constexpr int radius = 16;
    constexpr double pi = 3.14159265358979323846;

    const auto outLen = (size_t) std::ceil ((double) in.size() * ratio);
    std::vector<float> out (outLen, 0.0f);

    for (size_t n = 0; n < outLen; ++n)
    {
        const double srcPos = (double) n / ratio;
        const auto k0 = (long) std::floor (srcPos) - radius + 1;
        const auto k1 = (long) std::floor (srcPos) + radius;

        // when downsampling, widen the kernel to keep it anti-aliasing
        const double cutoff = std::min (1.0, ratio);

        double sum = 0.0;
        for (long k = k0; k <= k1; ++k)
        {
            if (k < 0 || k >= (long) in.size())
                continue;

            const double x = (srcPos - (double) k) * cutoff;
            const double sinc = (std::abs (x) < 1e-9) ? 1.0 : std::sin (pi * x) / (pi * x);
            const double w = 0.5 + 0.5 * std::cos (pi * (srcPos - (double) k) / (double) radius);
            sum += (double) in[(size_t) k] * sinc * cutoff * w;
        }

        out[n] = (float) sum;
    }

    return out;
}
} // namespace

bool loadIRFile (const std::string& path, double targetSampleRate, int maxSamples,
                 IRData& out, std::string& errorMessage)
{
    out = {};

    unsigned int channels = 0, sampleRate = 0;
    drwav_uint64 totalFrames = 0;
    float* interleaved = drwav_open_file_and_read_pcm_frames_f32 (path.c_str(), &channels, &sampleRate,
                                                                  &totalFrames, nullptr);
    if (interleaved == nullptr || channels == 0 || totalFrames == 0)
    {
        if (interleaved != nullptr)
            drwav_free (interleaved, nullptr);
        errorMessage = "Could not read WAV file: " + path;
        return false;
    }

    const auto numCh = std::min (2u, channels);

    // deinterleave (keeping at most 2 channels)
    std::vector<float> raw[2];
    for (unsigned int ch = 0; ch < numCh; ++ch)
    {
        raw[ch].resize ((size_t) totalFrames);
        for (drwav_uint64 i = 0; i < totalFrames; ++i)
            raw[ch][(size_t) i] = interleaved[i * channels + ch];
    }
    drwav_free (interleaved, nullptr);

    // resample to the host rate
    const double ratio = targetSampleRate / (double) sampleRate;
    for (unsigned int ch = 0; ch < numCh; ++ch)
    {
        if (std::abs (ratio - 1.0) > 1e-9)
            raw[ch] = resample (raw[ch], ratio);

        if ((int) raw[ch].size() > maxSamples)
            raw[ch].resize ((size_t) maxSamples);
    }

    // energy normalisation across channels
    double energy = 0.0;
    for (unsigned int ch = 0; ch < numCh; ++ch)
        for (float v : raw[ch])
            energy += (double) v * (double) v;
    energy /= (double) numCh;

    const auto gain = (float) (1.0 / std::sqrt (std::max (1e-12, energy)));
    for (unsigned int ch = 0; ch < numCh; ++ch)
        for (auto& v : raw[ch])
            v *= gain;

    out.numChannels = (int) numCh;
    out.length = (int) raw[0].size();
    for (unsigned int ch = 0; ch < numCh; ++ch)
        out.channels[ch] = std::move (raw[ch]);

    if (out.length < 1)
    {
        errorMessage = "Empty impulse response: " + path;
        out = {};
        return false;
    }

    return true;
}

} // namespace nsdsp
