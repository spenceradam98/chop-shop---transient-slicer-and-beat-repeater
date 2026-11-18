// The pattern player's decisions. Everything here is a pure function of the kit, the
// settings, the cycle number and the step, so a seed always gives the same pattern
// (and Lock makes every cycle identical).
#pragma once

#include "Kit.h"

namespace cs
{
enum OrderId { OrderOriginal = 0, OrderRandom, OrderReverse, OrderShuffle };

struct PatternSettings
{
    int order = OrderOriginal;
    float swing = 0.0f;       // 0..1 -> 50%..75% 16th swing
    float mutation = 1.0f;    // 0..2, scales every probability
    int seed = 1;
    bool lock = false;
};

struct StepPlan
{
    bool play = false;
    int slice = 0;
    bool reverse = false;
    bool repeat = false;
    int repeatRate = Rep32;
    bool drop = false;
    float dropDepth = 0.0f;
    float gain = 1.0f;         // linear, from the slice level
    float pitch = 0.0f;        // semitones
};

// Number of steps that fall inside the loop (slices starting before loopBeats).
int stepCount (const Kit&);

// Beat (within the loop) where step i fires, with swing applied.
double stepBeat (const Kit&, int step, float swing);

StepPlan planStep (const Kit&, const PatternSettings&, long long cycle, int step);

// Repeat rate in beats.
inline double repeatBeats (int rate) { return rate == Rep16 ? 0.25 : rate == Rep64 ? 0.0625 : 0.125; }
} // namespace cs
