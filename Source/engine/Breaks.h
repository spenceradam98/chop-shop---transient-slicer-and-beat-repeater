// Factory demo breaks. They are synthesised here from our own drum models and grooves,
// so they are original material (no sampled records).
#pragma once

#include "Kit.h"
#include <vector>

namespace cs
{
struct BreakInfo { const char* name; double bpm; int bars; };
const std::vector<BreakInfo>& factoryBreaks();
std::shared_ptr<Sample> renderBreak (int index, double sampleRate);
} // namespace cs
