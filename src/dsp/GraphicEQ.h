#pragma once

#include <array>

namespace nsdsp
{

// Five-band graphic equaliser modelled on the one built into the Mesa/Boogie
// Mark series (Mark IIC+ / III / IV), whose sliders are the documented set
//
//     80 Hz - 240 Hz - 750 Hz - 2200 Hz - 6600 Hz
//
// with a travel of about +/-12 dB.
//
// This is not a cascade of independent peaking filters. It is a model of the
// circuit those EQs actually use -- a single op-amp with one gyrator (a
// simulated series LC) per band -- because the interaction between the bands is
// the whole character of the thing.
//
// Topology. Each band has a slider pot of resistance Rp wired across the
// amplifier, from the input node to the output node; its wiper feeds the
// op-amp's summing node S through a series-resonant branch Z_i(s) = Rs + sL +
// 1/(sC), tuned to the band's centre. Rin and Rf set the flat gain. Off
// resonance Z_i is large and the band does nothing; at resonance it is small, so
// the wiper injects current into S -- drawn from the input end of the pot
// (boost) or from the output end (feedback, so cut).
//
// Writing the wiper's Thevenin source and summing the currents at S (a virtual
// earth) gives the response in closed form:
//
//     H(s) = - [ 1/Rin + SUM_i (1-k_i) Y_i(s) ] / [ 1/Rf + SUM_i k_i Y_i(s) ]
//
//     Y_i(s) = 1 / ( k_i(1-k_i)Rp + Rs + sL_i + 1/(sC_i) )
//
// with k_i the wiper position: 0 = full boost, 1/2 = flat, 1 = full cut. Every
// band appears in both the numerator and the denominator of the same fraction,
// and that single fact is what the cascade could not reproduce:
//
//   * the bands interact -- two adjacent boosts do not add, they compound
//     through the shared summing node;
//   * the Q is proportional, not constant: the pot's Thevenin resistance
//     k(1-k)Rp is largest at the centre and vanishes at the extremes, so a band
//     is broad and gentle near flat and tightens as it is pushed;
//   * engaging the EQ shifts the overall level, which is why the classic "V"
//     sounds the way it does.
//
// With every slider centred, H(s) = -Rf/Rin exactly, whatever the bands are
// doing -- the model is bit-flat when it should be. The output is negated so
// that flat is +1 rather than the circuit's inverting -1.
//
// The discretisation is topology-preserving: each branch is integrated with the
// trapezoidal rule and written as an instantaneous conductance plus a state
// term, which lets the summing node be solved directly each sample. There is no
// delay-free-loop approximation and no root finding.
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

    // gainsDb: numBands values, each in [-maxGainDb, +maxGainDb]. The value is
    // the band's own setting, i.e. what it would do on its own; once several
    // sliders leave the centre they pull on each other, exactly as the circuit
    // does, so the response is not simply the sum of them.
    void setGains (const float* gainsDb);

    void processBlock (float* data, int numSamples);

private:
    void updateCoefficients();

    // Wiper position that puts this band at `gainDb` with the other four
    // centred, found by bisecting the closed-form response (monotonic in k).
    static double wiperForGain (double gainDb, int band);

    struct Band
    {
        // component values, fixed once the sample rate is known
        double gL = 0;  // T / (2L)
        double gC = 0;  // T / (2C)

        // wiper, and what it implies for the branch (recomputed on a gain change)
        double k = 0.5;
        double G = 0;   // instantaneous conductance of the branch
        double a1 = 0;  // state weights: S = a1*prevI + G*prevV - 2G*prevVc

        // trapezoidal state
        double prevI = 0, prevV = 0, prevVc = 0;
    };

    double fs = 48000.0;
    std::array<float, numBands> gains {}; // dB, zero-initialised = flat
    std::array<Band, numBands> bands {};

    // node sums, recomputed with the wipers
    double yNum = 0; // 1/Rin + SUM G_i (1 - k_i)
    double yDen = 0; // 1/Rf   + SUM G_i k_i
    double invYDen = 0;

    bool dirty = true;
};

} // namespace nsdsp
