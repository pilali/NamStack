#pragma once

#include <array>

namespace nsdsp
{

// Five-band graphic equaliser modelled on the one built into the Mesa/Boogie
// Mark series (Mark IIC+ / III / IV), whose sliders are the documented set
//
//     80 Hz - 240 Hz - 750 Hz - 2200 Hz - 6600 Hz
//
// with a travel of about +/-12 dB. The bands sit a little over 1.5 octaves
// apart (each centre is roughly 3x the previous one), which fixes the width of
// each band: for a bandwidth of N octaves the usual graphic-EQ relation
//
//     Q = 2^(N/2) / (2^N - 1)
//
// gives Q ~= 0.87 for N = log2(3) ~= 1.585. Each band is a Robert Bristow-
// Johnson peaking biquad and the five are run in series.
//
// The real circuit is a passive interacting network whose bands pull on each
// other and which also loses level as it is engaged (this is why the classic
// "V" setting sounds the way it does). A cascade of independent peaking filters
// is the standard approximation and is what the documented slider frequencies
// describe; it does not reproduce that interaction.
class GraphicEQ
{
public:
    static constexpr int numBands = 5;

    // Slider centre frequencies, in Hz.
    static const std::array<float, numBands>& getFrequencies();

    // Slider travel, in dB (the sliders run from -maxGainDb to +maxGainDb).
    static constexpr float maxGainDb = 12.0f;

    void prepare (double sampleRate);
    void reset();

    // gainsDb: numBands values, each in [-maxGainDb, +maxGainDb]
    void setGains (const float* gainsDb);

    void processBlock (float* data, int numSamples);

private:
    struct Biquad
    {
        // b0 .. a2 with a0 normalised to 1
        double b0 = 1, b1 = 0, b2 = 0;
        double a1 = 0, a2 = 0;

        // direct form II transposed state
        double z1 = 0, z2 = 0;
    };

    void updateCoefficients();

    double fs = 48000.0;
    std::array<float, numBands> gains {}; // dB, zero-initialised = flat
    std::array<Biquad, numBands> bands {};
    bool dirty = true;
};

} // namespace nsdsp
