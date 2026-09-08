#include "ToneStack.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace nsdsp
{

namespace
{
// Band over which the makeup gain is measured, and how finely. Pink weighting
// (equal weight per octave) is what a log-spaced grid with a flat average
// gives for free, and it matches how a guitar signal spreads its energy far
// better than a linear one -- a linear grid would let the 4-8 kHz region,
// where nothing much lives after a cabinet IR, decide the level.
constexpr double kMeasureLoHz = 80.0;
constexpr double kMeasureHiHz = 8000.0;
constexpr int kMeasureBins = 64;

// Ceiling on the makeup. Nothing in the shipped model set gets near it (the
// largest noon makeup is the Twin Reverb's +13.1 dB); it is only there so a
// pathological component set cannot turn the stack into a noise amplifier.
constexpr double kMaxMakeupDb = 24.0;

constexpr double kPi = 3.14159265358979323846;
} // namespace

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
    computeMakeupGains();
    ramp.prepare (sampleRate);
    dirty = true;
    reset();
}

void ToneStack::setEngaged (bool engaged, bool pre)
{
    const auto haveCircuit = currentModel > bypass && currentModel < numModels;

    if (! (engaged && haveCircuit))
    {
        // Switched off, or the selector moved to Bypass. Either way fade out
        // through the amplifier that is still being heard rather than cutting
        // to dry: updateCoefficients() leaves `coeffs` alone for the bypass
        // entry, so the coefficients of the last real circuit are still there.
        ramp.bypassKeepingIdentity();
        return;
    }

    // Identity: the amplifier and the tap point. A change of either makes the
    // running state meaningless -- it belongs to another circuit, or to
    // another point of the chain -- so the ramp fades the old one out, clears
    // it, and fades the new one in.
    ramp.setEngaged (true, currentModel * 2 + (pre ? 0 : 1));

    // Only follow the knob once the swap has landed; until then the fade-out
    // has to keep running where its state came from.
    if (! ramp.isSwapping())
        activePre = pre;
}

void ToneStack::reset()
{
    z1 = z2 = z3 = 0.0;
}

void ToneStack::setParams (int model, float bass, float mid, float treble, bool levelComp)
{
    if (model != currentModel || bass != bassKnob || mid != midKnob || treble != trebleKnob
        || levelComp != compensate)
    {
        // A model change used to reset() here. The ramp does it now, on the
        // block that fades the new amplifier in, so the outgoing one keeps its
        // state for as long as it is still being heard.
        currentModel = model;
        bassKnob = bass;
        midKnob = mid;
        trebleKnob = treble;
        compensate = levelComp;
        dirty = true;
    }
}

float ToneStack::getMakeupGain (int model) const
{
    if (model <= bypass || model >= numModels)
        return 1.0f;

    return (float) makeup[(size_t) model];
}

ToneStack::Coefficients ToneStack::computeCoefficients (const Components& c, double fs,
                                                        double bass, double mid, double treble)
{
    const double R1 = c.R1, R2 = c.R2, R3 = c.R3, R4 = c.R4;
    const double C1 = c.C1, C2 = c.C2, C3 = c.C3;

    // The bass pot in these circuits is logarithmic (audio) taper; squaring
    // the knob value is the customary approximation. Mid and treble pots are
    // linear.
    const double l = bass * bass;
    const double m = mid;
    const double t = treble;

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

    Coefficients co;
    co.b0 = B0d * norm;
    co.b1 = B1d * norm;
    co.b2 = B2d * norm;
    co.b3 = B3d * norm;
    co.a1 = A1d * norm;
    co.a2 = A2d * norm;
    co.a3 = A3d * norm;
    return co;
}

double ToneStack::magnitudeAt (const Coefficients& co, double frequency, double sampleRate)
{
    const std::complex<double> z = std::polar (1.0, -2.0 * kPi * frequency / sampleRate);
    const std::complex<double> z2 = z * z;
    const std::complex<double> z3 = z2 * z;
    const auto num = co.b0 + co.b1 * z + co.b2 * z2 + co.b3 * z3;
    const auto den = 1.0 + co.a1 * z + co.a2 * z2 + co.a3 * z3;
    return std::abs (num / den);
}

void ToneStack::computeMakeupGains()
{
    makeup.fill (1.0);

    // Keep the top of the measurement band below Nyquist so the makeup stays
    // meaningful at low host rates.
    const double hi = std::min (kMeasureHiHz, fs * 0.45);
    if (hi <= kMeasureLoHz)
        return;

    const double maxMakeup = std::pow (10.0, kMaxMakeupDb / 20.0);
    const double ratio = hi / kMeasureLoHz;

    for (int model = bypass + 1; model < numModels; ++model)
    {
        // Reference setting: every knob at noon, the position a player starts
        // from and the one the on/off switch is judged against.
        const auto co = computeCoefficients (getModels()[(size_t) model], fs, 0.5, 0.5, 0.5);

        double power = 0.0;
        for (int bin = 0; bin < kMeasureBins; ++bin)
        {
            const double f = kMeasureLoHz * std::pow (ratio, (double) bin / (kMeasureBins - 1));
            const double g = magnitudeAt (co, f, fs);
            power += g * g;
        }

        const double rms = std::sqrt (power / (double) kMeasureBins);
        makeup[(size_t) model] = rms > 1.0e-9 ? std::min (1.0 / rms, maxMakeup) : 1.0;
    }
}

void ToneStack::updateCoefficients()
{
    dirty = false;

    if (currentModel <= bypass || currentModel >= numModels)
        return;

    coeffs = computeCoefficients (getModels()[(size_t) currentModel], fs,
                                  (double) bassKnob, (double) midKnob, (double) trebleKnob);

    // The makeup is constant per model, so folding it into the numerator keeps
    // the audio loop exactly as cheap as it was before compensation existed.
    appliedMakeup = compensate ? makeup[(size_t) currentModel] : 1.0;
    coeffs.b0 *= appliedMakeup;
    coeffs.b1 *= appliedMakeup;
    coeffs.b2 *= appliedMakeup;
    coeffs.b3 *= appliedMakeup;
}

void ToneStack::processBlock (float* data, int numSamples)
{
    if (! ramp.isRunning())
        return;

    if (ramp.takeClearRequest())
        reset();

    // While a swap is fading out, the coefficients must stay those of the
    // amplifier still being heard; the update lands on the block that fades
    // the new one in.
    if (dirty && ! ramp.isSwapping())
        updateCoefficients();

    // No test for the bypass model here: `coeffs` still holds the last real
    // circuit's, which is exactly what a fade-out to Bypass has to run
    // through. Before anything has ever been engaged they are the identity
    // (b0 = 1), and the ramp is not running anyway.

    // Settled at full wet: the plain in-place loop, exactly as before the ramp
    // existed. The blend below is only paid during the 25 ms itself.
    if (! ramp.isFading())
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const double x = (double) data[i];
            const double y = coeffs.b0 * x + z1;
            z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
            z2 = coeffs.b2 * x - coeffs.a2 * y + z3;
            z3 = coeffs.b3 * x - coeffs.a3 * y;
            data[i] = (float) y;
        }
        return;
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const double x = (double) data[i];
        const double y = coeffs.b0 * x + z1;
        z1 = coeffs.b1 * x - coeffs.a1 * y + z2;
        z2 = coeffs.b2 * x - coeffs.a2 * y + z3;
        z3 = coeffs.b3 * x - coeffs.a3 * y;

        const double g = (double) ramp.next();
        data[i] = (float) (x + (y - x) * g);
    }

    // The fade-out landed: leave no state behind for the next engage.
    if (! ramp.isRunning())
        reset();
}

} // namespace nsdsp
