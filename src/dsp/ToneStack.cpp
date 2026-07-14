#include "ToneStack.h"

#include <cmath>

namespace nsdsp
{

const std::array<ToneStack::Components, ToneStack::numModels>& ToneStack::getModels()
{
    static const std::array<Components, numModels> models = { {
        { "Bypass",                 0,      0,   0,      0,    0,       0,      0 },
        { "Fender Bassman 5F6-A",   250e3,  1e6, 25e3,   56e3, 250e-12, 20e-9,  20e-9 },
        { "Fender Twin Reverb",     250e3,  250e3, 10e3, 100e3, 120e-12, 100e-9, 47e-9 },
        { "Fender Princeton",       250e3,  250e3, 4.8e3, 100e3, 250e-12, 100e-9, 47e-9 },
        { "Marshall JCM800 2203",   220e3,  1e6, 22e3,   33e3, 470e-12, 22e-9,  22e-9 },
        { "Mesa Boogie Mark",       250e3,  250e3, 25e3, 100e3, 250e-12, 100e-9, 47e-9 },
        { "Vox AC30 Top Boost",     1e6,    1e6, 10e3,   100e3, 50e-12,  22e-9,  22e-9 },
        { "Ampeg SVT",              250e3,  1e6, 25e3,   32e3, 470e-12, 22e-9,  22e-9 },
        { "Soldano SLO-100",        250e3,  1e6, 25e3,   47e3, 470e-12, 20e-9,  20e-9 },
        // The DR103's stack is not the Fender topology this class models -- its
        // treble side is a bridged pair (1nF + 220pF with 220k across), the mid
        // sits above the bass in the chain and the bass pot is straddled by two
        // 47nF from the 100k feed -- so its values cannot simply be copied in.
        // What carries over literally (DR103 preamp schematic, issue 4): 220k
        // treble pot, 470k bass pot, the 100k feed as the slope resistor, 180pF
        // treble cap (1nF in series with 220pF) and a 47nF mid cap. The mid
        // resistance and bass cap are then fitted (33k, 22nF) so this topology
        // reproduces the circuit's character: a ~4dB mid dip at noon where the
        // Fenders sit at 8-12dB, mids up flattens it out, and bass/treble reach
        // comparable to the other models here.
        { "Hiwatt DR103",           220e3,  470e3, 33e3,  100e3, 180e-12, 22e-9, 47e-9 },
    } };
    return models;
}

void ToneStack::prepare (double sampleRate)
{
    fs = sampleRate;
    dirty = true;
    reset();
}

void ToneStack::reset()
{
    z1 = z2 = z3 = 0.0;
}

void ToneStack::setParams (int model, float bass, float mid, float treble)
{
    if (model != currentModel || bass != bassKnob || mid != midKnob || treble != trebleKnob)
    {
        if (model != currentModel)
            reset();

        currentModel = model;
        bassKnob = bass;
        midKnob = mid;
        trebleKnob = treble;
        dirty = true;
    }
}

void ToneStack::updateCoefficients()
{
    dirty = false;

    if (currentModel <= bypass || currentModel >= numModels)
        return;

    const auto& c = getModels()[(size_t) currentModel];

    const double R1 = c.R1, R2 = c.R2, R3 = c.R3, R4 = c.R4;
    const double C1 = c.C1, C2 = c.C2, C3 = c.C3;

    // The bass pot in these circuits is logarithmic (audio) taper; squaring
    // the knob value is the customary approximation. Mid and treble pots are
    // linear.
    const double l = (double) bassKnob * (double) bassKnob;
    const double m = (double) midKnob;
    const double t = (double) trebleKnob;

    // Analog transfer function coefficients (Yeh & Smith, DAFx-06).
    const double B1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);

    const double B2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4)
                    - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                    + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);

    const double B3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + t * C1 * C2 * C3 * R1 * R3 * R4
                    - t * m * C1 * C2 * C3 * R1 * R3 * R4
                    + t * l * C1 * C2 * C3 * R1 * R2 * R4;

    const double A0 = 1.0;

    const double A1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4)
                    + m * C3 * R3
                    + l * (C1 * R2 + C2 * R2);

    const double A2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
                    - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
                    + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
                    + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4
                       + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);

    const double A3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
                    - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
                    + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
                    + l * C1 * C2 * C3 * R1 * R2 * R4
                    + C1 * C2 * C3 * R1 * R3 * R4;

    // Bilinear transform  s = c (1 - z^-1) / (1 + z^-1),  c = 2 fs
    const double cbt = 2.0 * fs;
    const double c2 = cbt * cbt;
    const double c3 = c2 * cbt;

    const double B0d = B1 * cbt + B2 * c2 + B3 * c3;
    const double B1d = B1 * cbt - B2 * c2 - 3.0 * B3 * c3;
    const double B2d = -B1 * cbt - B2 * c2 + 3.0 * B3 * c3;
    const double B3d = -B1 * cbt + B2 * c2 - B3 * c3;

    const double A0d = A0 + A1 * cbt + A2 * c2 + A3 * c3;
    const double A1d = 3.0 * A0 + A1 * cbt - A2 * c2 - 3.0 * A3 * c3;
    const double A2d = 3.0 * A0 - A1 * cbt - A2 * c2 + 3.0 * A3 * c3;
    const double A3d = A0 - A1 * cbt + A2 * c2 - A3 * c3;

    const double norm = 1.0 / A0d;
    b0 = B0d * norm;
    b1 = B1d * norm;
    b2 = B2d * norm;
    b3 = B3d * norm;
    a1 = A1d * norm;
    a2 = A2d * norm;
    a3 = A3d * norm;
}

void ToneStack::processBlock (float* data, int numSamples)
{
    if (dirty)
        updateCoefficients();

    if (currentModel <= bypass || currentModel >= numModels)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        const double x = (double) data[i];
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y + z3;
        z3 = b3 * x - a3 * y;
        data[i] = (float) y;
    }
}

} // namespace nsdsp
