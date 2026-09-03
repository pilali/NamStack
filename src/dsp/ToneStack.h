#pragma once

#include "BypassRamp.h"

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
//
// Insertion loss and the level compensation
// -----------------------------------------
// These circuits are *passive*: their transfer function never exceeds 0 dB,
// and at the knob positions people actually use they sit well below it. At
// noon (0.5 / 0.5 / 0.5) the band-weighted loss runs from -4.7 dB (Ampeg
// SVT) to -13.1 dB (Fender Twin Reverb) -- in the real amplifier that is
// made up by the gain stage the stack drives, which is exactly what a plugin
// chain does not have. Switching the stack on therefore dropped the level
// hard, and switching models moved it again.
//
// The levelComp flag of setParams() (on by default) folds a per-model makeup
// gain into the filter so that *the noon setting of every model is 0 dB* on a pink-weighted
// average over 80 Hz - 8 kHz. Turning a knob still moves the level exactly as
// the circuit does -- bass/mid/treble all the way down is still quieter, all
// the way up still louder -- and switching models no longer jumps. The makeup
// is a single constant per model, computed once in prepare(); it costs one
// multiply per sample.
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

    // Per block, before processBlock(). Engaging and bypassing are ~25 ms
    // crossfades rather than hard switches, and so is a move to the other side
    // of the neural model or a change of amplifier: the filter's state belongs
    // to the point it was reading and the circuit it was modelling. See
    // BypassRamp. `pre` is which side of the model this block runs on.
    void setEngaged (bool engaged, bool pre);

    // False = settled in bypass; processBlock() is a no-op and can be skipped.
    bool isRunning() const { return ramp.isRunning(); }

    // Which side of the model processBlock() must be called on *now*. During
    // the fade-out half of a pre/post move this is still the old side: the
    // state being faded out belongs to the signal it was reading, so that is
    // where it has to be run.
    bool runsPre() const { return activePre; }

    // model: one of Model, knobs in [0, 1]. levelComp folds in the per-model
    // makeup gain described above.
    void setParams (int model, float bass, float mid, float treble, bool levelComp = true);

    // Makeup gain (linear) that puts the model's noon setting at 0 dB. 1 for
    // bypass and out-of-range indices. Valid after prepare().
    float getMakeupGain (int model) const;

    void processBlock (float* data, int numSamples);

private:
    // Digital coefficients of one setting, a0 normalised to 1.
    struct Coefficients
    {
        double b0 = 1, b1 = 0, b2 = 0, b3 = 0;
        double a1 = 0, a2 = 0, a3 = 0;
    };

    static Coefficients computeCoefficients (const Components& c, double fs,
                                             double bass, double mid, double treble);
    static double magnitudeAt (const Coefficients& co, double frequency, double sampleRate);

    void updateCoefficients();
    void computeMakeupGains();

    double fs = 48000.0;
    int currentModel = bypass;
    float bassKnob = 0.5f, midKnob = 0.5f, trebleKnob = 0.5f;
    bool compensate = true;
    bool dirty = true;

    Coefficients coeffs;

    // Per-model noon makeup, filled by prepare(). Index 0 (bypass) stays 1.
    std::array<double, numModels> makeup { {} };
    double appliedMakeup = 1.0;

    BypassRamp ramp;
    bool activePre = false;

    // direct form II transposed state
    double z1 = 0, z2 = 0, z3 = 0;
};

} // namespace nsdsp
