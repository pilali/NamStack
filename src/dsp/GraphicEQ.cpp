#include "GraphicEQ.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace nsdsp
{

namespace
{
constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------- components
// Rin and Rf are equal, so the flat gain is exactly -1 and the sliders are
// symmetric around it.
constexpr double kR = 10.0e3;    // Rin = Rf
constexpr double kRp = 100.0e3;  // slider pot, as on the amp

// Q of the resonant branch with the wiper hard over. This is the one value the
// topology does not hand us: it is set by the gyrator's L/C ratio on the real
// board. At 1.6 a fully pushed band is about 1.5 octaves wide at half its
// height, near the 1.585-octave spacing of the five centres, so neighbouring
// bands meet where they cross rather than leaving holes between them.
constexpr double kBranchQ = 1.6;
} // namespace

const std::array<float, GraphicEQ::numBands>& GraphicEQ::getFrequencies()
{
    static const std::array<float, numBands> frequencies {
        80.0f, 240.0f, 750.0f, 2200.0f, 6600.0f
    };
    return frequencies;
}

namespace
{
// Branch admittance Y(jw) with the wiper at k, for a band centred on w0.
std::complex<double> bandAdmittance (double w, double w0, double k, double rs)
{
    const double rTot = rs + k * (1.0 - k) * kRp;
    const double L = kBranchQ * rs / w0;
    const double C = 1.0 / (w0 * w0 * L);

    // Z = rTot + jwL + 1/(jwC)
    const std::complex<double> z (rTot, w * L - 1.0 / (w * C));
    return 1.0 / z;
}

// Everything the four centred neighbours present at band `i`'s centre, plus the
// amplifier's own 1/Rin. Constant for a given Rs.
std::complex<double> nodeLoadAt (int i, double rs)
{
    const auto& f = GraphicEQ::getFrequencies();
    const double w = 2.0 * kPi * (double) f[(size_t) i];

    std::complex<double> load (1.0 / kR, 0.0);
    for (int j = 0; j < GraphicEQ::numBands; ++j)
        if (j != i)
            load += 0.5 * bandAdmittance (w, 2.0 * kPi * (double) f[(size_t) j], 0.5, rs);
    return load;
}

// Boost a band reaches with its wiper hard over (k = 0), in dB.
double maxBoostDb (int i, double rs)
{
    const auto load = nodeLoadAt (i, rs);
    return 20.0 * std::log10 (std::abs (load + 1.0 / rs) / std::abs (load));
}

// The branch's series resistance sets the travel: hard over, the branch
// impedance is Rs, and the smaller it is the more current the wiper injects.
//
// Rs cannot simply be read off 1 + R/Rs = 12 dB, because the four other branches
// are physically present and load the summing node even when centred, which
// pulls every band's reach down by a decibel or two. So solve it: pick the Rs at
// which the *least* favoured band still just reaches the full +12 dB. The others
// can then overshoot slightly at k = 0, and their wiper simply lands a little
// short of the end -- which is what a trimmed EQ does anyway. Rs does not
// disturb the band shapes: the branch Q is w0*L/Rs = kBranchQ by construction.
double solveRs()
{
    auto worstBand = [] (double rs)
    {
        double worst = 1e9;
        for (int i = 0; i < GraphicEQ::numBands; ++i)
            worst = std::min (worst, maxBoostDb (i, rs));
        return worst;
    };

    // maxBoost falls as Rs grows, so bisect.
    double lo = 1.0, hi = 100.0e3;
    for (int n = 0; n < 200; ++n)
    {
        const double mid = 0.5 * (lo + hi);
        if (worstBand (mid) > (double) GraphicEQ::maxGainDb)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

const double kRs = solveRs();
} // namespace

// The dB a slider is labelled with is what that band does with the other four
// centred -- the reference condition. That is not the same as the band on its
// own: the other branches are physically there, and even centred they load the
// summing node and pull the boost back. So the wiper is solved against the full
// response with the neighbours in place, which makes the readout exact in the
// reference case. Move a second slider and the bands pull on each other, which
// is the entire point of the model.
double GraphicEQ::wiperForGain (double gainDb, int band)
{
    const double target = std::pow (10.0, gainDb / 20.0);

    // What the four centred neighbours, and the amplifier itself, present at
    // this band's centre.
    const auto neighbours = nodeLoadAt (band, kRs);

    auto gainAt = [&] (double k)
    {
        // On its own centre the band's admittance is real: 1 / rTot.
        const double y = 1.0 / (kRs + k * (1.0 - k) * kRp);
        return std::abs (neighbours + (1.0 - k) * y) / std::abs (neighbours + k * y);
    };

    // Monotonically decreasing over [0, 1]: k = 0 is full boost, k = 1 full cut.
    double lo = 0.0, hi = 1.0;
    for (int i = 0; i < 60; ++i)
    {
        const double mid = 0.5 * (lo + hi);
        if (gainAt (mid) > target)
            lo = mid;
        else
            hi = mid;
    }
    return 0.5 * (lo + hi);
}

void GraphicEQ::prepare (double sampleRate)
{
    ramp.prepare (sampleRate);
    fs = sampleRate;

    const double T = 1.0 / fs;
    const auto& frequencies = getFrequencies();

    for (int i = 0; i < numBands; ++i)
    {
        const double w0 = 2.0 * kPi * (double) frequencies[(size_t) i];

        // Branch Q with the wiper hard over is w0 * L / Rs, which fixes L, and
        // the centre frequency then fixes C.
        const double L = kBranchQ * kRs / w0;
        const double C = 1.0 / (w0 * w0 * L);

        auto& band = bands[(size_t) i];
        band.gL = T / (2.0 * L);
        band.gC = T / (2.0 * C);
    }

    dirty = true;
    reset();
}

void GraphicEQ::reset()
{
    for (auto& band : bands)
        band.prevI = band.prevV = band.prevVc = 0.0;
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
    yNum = 1.0 / kR;
    yDen = 1.0 / kR;

    for (int i = 0; i < numBands; ++i)
    {
        auto& band = bands[(size_t) i];

        band.k = wiperForGain ((double) gains[(size_t) i], i);

        // Total series resistance of the branch: the gyrator's own Rs plus what
        // the pot presents at its wiper. That second term is what makes the Q
        // proportional -- k(1-k)Rp peaks at the centre and vanishes at the ends.
        const double rTot = kRs + band.k * (1.0 - band.k) * kRp;

        // Trapezoidal integration of  V = R*I + L*dI/dt + Vc,  dVc/dt = I/C,
        // rearranged to  I[n] = G*V[n] + S,  with S built from the state alone.
        const double den = 1.0 + band.gL * rTot + band.gL * band.gC;
        band.G = band.gL / den;
        band.a1 = (1.0 - band.gL * band.gC - band.gL * rTot) / den;

        yNum += band.G * (1.0 - band.k);
        yDen += band.G * band.k;
    }

    invYDen = 1.0 / yDen; // the sample loop must not divide
    dirty = false;
}

void GraphicEQ::setEngaged (bool engaged, bool pre)
{
    ramp.setEngaged (engaged, pre ? 0 : 1);

    // Only follow the knob once the swap has landed; until then the fade-out
    // has to keep running where its state came from.
    if (! ramp.isSwapping())
        activePre = pre;
}

void GraphicEQ::processBlock (float* data, int numSamples)
{
    if (! ramp.isRunning())
        return;

    if (ramp.takeClearRequest())
        reset();

    if (dirty)
        updateCoefficients();

    // Settled at full wet the blend below folds to `out`, so the branch is
    // hoisted out of the sample loop rather than tested per sample.
    const auto fading = ramp.isFading();

    for (int n = 0; n < numSamples; ++n)
    {
        const double vin = (double) data[n];

        // Each branch's contribution to the summing node splits into a term that
        // depends on this sample's voltage (through G) and one that does not.
        double sSum = 0.0;
        double s[numBands];

        for (int i = 0; i < numBands; ++i)
        {
            auto& band = bands[(size_t) i];
            s[i] = band.a1 * band.prevI + band.G * band.prevV - 2.0 * band.G * band.prevVc;
            sSum += s[i];
        }

        // Summing node, solved directly: no delay-free loop is left over.
        // The circuit inverts; negate so that flat is +1 rather than -1.
        const double out = (vin * yNum + sSum) * invYDen;
        const double vout = -out;

        for (int i = 0; i < numBands; ++i)
        {
            auto& band = bands[(size_t) i];

            // wiper voltage, now that both ends of the pot are known
            const double v = (1.0 - band.k) * vin + band.k * vout;
            const double current = band.G * v + s[i];

            band.prevVc += band.gC * (current + band.prevI);
            band.prevI = current;
            band.prevV = v;
        }

        data[n] = fading ? (float) (vin + (out - vin) * (double) ramp.next())
                         : (float) out;
    }

    // The fade-out landed: leave no state behind for the next engage.
    if (! ramp.isRunning())
        reset();
}

} // namespace nsdsp
