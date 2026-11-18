// Pattern presets: they set the pattern / global parameters and write per-slice
// probabilities from each slice's position in the bar, so they work on any break.
#pragma once

#include "Kit.h"
#include "Params.h"
#include <array>
#include <vector>

namespace cs
{
struct PatternPreset
{
    const char* name;
    const char* description;
    std::vector<std::pair<int, float>> values;   // parameters that differ from the defaults
    int rule;                                    // which per-slice rule to apply
};

const std::vector<PatternPreset>& patternPresets();

// Full parameter set for a preset (defaults overlaid with its values). Slicing and mapping
// parameters are left as they are in `current`.
std::array<float, NumParams> presetValues (const PatternPreset&, const std::array<float, NumParams>& current);

// Rewrites the probability fields of the kit's per-slice settings for this preset.
void applyPresetRule (const PatternPreset&, Kit&);
} // namespace cs
