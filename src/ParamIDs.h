#pragma once

#include <juce_core/juce_core.h>

namespace ParamIDs
{
inline constexpr auto inputGain = "in_gain";
inline constexpr auto outputGain = "out_gain";

// Conditioning inputs of conditioned AIDA-X models (gain/master style
// controls baked into the training)
inline constexpr auto aidaParam1 = "aida_p1";
inline constexpr auto aidaParam2 = "aida_p2";

// Quality vs CPU trade-off of slimmable NAM models (A2)
inline constexpr auto modelQuality = "model_quality";

inline constexpr auto tsOn = "ts_on";
inline constexpr auto tsModel = "ts_model";
inline constexpr auto tsPosition = "ts_position";
inline constexpr auto tsBass = "ts_bass";
inline constexpr auto tsMid = "ts_mid";
inline constexpr auto tsTreble = "ts_treble";

// Mesa/Boogie-style 5-band graphic EQ. Band ids are geq_80 .. geq_6600, built
// from the centre frequencies in nsdsp::GraphicEQ so the two cannot drift apart.
inline constexpr auto geqOn = "geq_on";
inline constexpr auto geqPosition = "geq_position";
inline juce::String geqBand (int band, float frequencyHz)
{
    juce::ignoreUnused (band);
    return "geq_" + juce::String (juce::roundToInt (frequencyHz));
}

// Per-IR-slot ids are built as ir<N>_on / ir<N>_gain / ir<N>_pan (N = 1..4)
inline juce::String irOn(int slot) { return "ir" + juce::String(slot + 1) + "_on"; }
inline juce::String irGain(int slot) { return "ir" + juce::String(slot + 1) + "_gain"; }
inline juce::String irPan(int slot) { return "ir" + juce::String(slot + 1) + "_pan"; }

inline constexpr auto dblOn = "dbl_on";
inline constexpr auto dblMix = "dbl_mix";
inline constexpr auto dblTime = "dbl_time";
inline constexpr auto dblDetune = "dbl_detune";
inline constexpr auto dblHumanize = "dbl_humanize";
inline constexpr auto dblWidth = "dbl_width";
} // namespace ParamIDs
