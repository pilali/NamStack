#include "GraphicEQ.h"

#include <algorithm>
#include <cmath>

namespace nsdsp
{

namespace
{
// Bandwidth of one band, in octaves: the centres are spaced by a factor of
// about 3, i.e. log2(3) octaves apart.
constexpr double kBandwidthOctaves = 1.5849625007211562; // log2(3)

// Q for a graphic-EQ band of the above bandwidth:
//     Q = 2^(N/2) / (2^N - 1)
const double kQ = std::pow (2.0, kBandwidthOctaves * 0.5)
                / (std::pow (2.0, kBandwidthOctaves) - 1.0);
} // namespace

const std::array<float, GraphicEQ::numBands>& GraphicEQ::getFrequencies()
{
    static const std::array<float, numBands> frequencies {
        80.0f, 240.0f, 750.0f, 2200.0f, 6600.0f
    };
    return frequencies;
}

void GraphicEQ::prepare (double sampleRate)
{
    fs = sampleRate;
    dirty = true;
    reset();
}

void GraphicEQ::reset()
{
    for (auto& band : bands)
        band.z1 = band.z2 = 0.0;
}

void GraphicEQ::setGains (const float* gainsDb)
{
    for (int i = 0; i < numBands; ++i)
    {
        const auto g = std::clamp (gainsDb[i], -maxGainDb, maxGainDb);
        if (g != gains[(size_t) i])
        {
            gains[(size_t) i] = g;
            dirty = true;
        }
    }
}

void GraphicEQ::updateCoefficients()
{
    const auto& frequencies = getFrequencies();

    for (int i = 0; i < numBands; ++i)
    {
        auto& band = bands[(size_t) i];

        // Robert Bristow-Johnson peaking EQ. A is the square root of the
        // linear gain, so that the response reaches exactly `gain` dB at f0.
        const double A = std::pow (10.0, gains[(size_t) i] / 40.0);
        const double w0 = 2.0 * M_PI * (double) frequencies[(size_t) i] / fs;
        const double cosw0 = std::cos (w0);
        const double alpha = std::sin (w0) / (2.0 * kQ);

        const double b0 = 1.0 + alpha * A;
        const double b1 = -2.0 * cosw0;
        const double b2 = 1.0 - alpha * A;
        const double a0 = 1.0 + alpha / A;
        const double a1 = -2.0 * cosw0;
        const double a2 = 1.0 - alpha / A;

        band.b0 = b0 / a0;
        band.b1 = b1 / a0;
        band.b2 = b2 / a0;
        band.a1 = a1 / a0;
        band.a2 = a2 / a0;
    }

    dirty = false;
}

void GraphicEQ::processBlock (float* data, int numSamples)
{
    if (dirty)
        updateCoefficients();

    for (auto& band : bands)
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = (double) data[i];
            const double y = band.b0 * x + band.z1;
            band.z1 = band.b1 * x - band.a1 * y + band.z2;
            band.z2 = band.b2 * x - band.a2 * y;
            data[i] = (float) y;
        }
    }
}

} // namespace nsdsp
