#pragma once

#include <array>

namespace nsdsp
{

// Passive guitar-amp tone stack simulation.
//
// Continuous-time transfer function of the classic 3-knob (bass / mid /
// treble) passive tone stack, after D. T. Yeh and J. O. Smith,
// "Discretization of the '59 Fender Bassman Tone Stack" (DAFx-06),
// discretized with the bilinear transform. Component values for the
// individual amplifier models are the well documented sets used by the
// Duncan Tone Stack Calculator, guitarix and the Faust libraries.
class ToneStack
{
public:
    struct Components
    {
        const char* name;
        double R1, R2, R3, R4; // ohms
        double C1, C2, C3;     // farads
    };

    enum Model
    {
        bypass = 0,
        bassman,      // Fender Bassman 5F6-A
        twinReverb,   // Fender Twin Reverb AA769
        princeton,    // Fender Princeton AA964
        jcm800,       // Marshall JCM800 2203
        boogieMark,   // Mesa Boogie Mark series
        ac30,         // Vox AC30 top boost
        svt,          // Ampeg SVT
        slo,          // Soldano SLO-100
        hiwatt,       // Hiwatt DR103 (Custom 100). Appended: the model index
                      // is a saved parameter value, inserting would remap it.
        numModels
    };

    static const std::array<Components, numModels>& getModels();

    void prepare (double sampleRate);
    void reset();

    // model: one of Model, knobs in [0, 1]
    void setParams (int model, float bass, float mid, float treble);

    void processBlock (float* data, int numSamples);

private:
    void updateCoefficients();

    double fs = 48000.0;
    int currentModel = bypass;
    float bassKnob = 0.5f, midKnob = 0.5f, trebleKnob = 0.5f;
    bool dirty = true;

    // digital coefficients (3rd order, a0 normalised to 1)
    double b0 = 1, b1 = 0, b2 = 0, b3 = 0;
    double a1 = 0, a2 = 0, a3 = 0;

    // direct form II transposed state
    double z1 = 0, z2 = 0, z3 = 0;
};

} // namespace nsdsp
