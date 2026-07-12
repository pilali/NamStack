#pragma once

#include <juce_core/juce_core.h>

namespace ParamIDs
{
inline constexpr auto inputGain = "in_gain";
inline constexpr auto outputGain = "out_gain";

inline constexpr auto tsModel = "ts_model";
inline constexpr auto tsPosition = "ts_position";
inline constexpr auto tsBass = "ts_bass";
inline constexpr auto tsMid = "ts_mid";
inline constexpr auto tsTreble = "ts_treble";

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
