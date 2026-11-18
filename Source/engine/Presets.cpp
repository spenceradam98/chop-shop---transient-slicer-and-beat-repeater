#include "Presets.h"

#include <cmath>

namespace cs
{
enum Rule { RuleClean = 0, RuleJungle, RuleStutterFills, RuleHalfTime, RuleReverseGhosts, RuleHipHopSkips, RuleTapeStopEnds, RuleRain };

const std::vector<PatternPreset>& patternPresets()
{
    static const std::vector<PatternPreset> list = {
        { "Straight Chop", "The break as it was, in time with the host.", { { Order, 0 }, { Mutation, 1.0f } }, RuleClean },
        { "Jungle Mutate", "Slices shuffle inside each bar with fast stutters and the odd reversal.",
          { { Order, 3 }, { Mutation, 1.2f } }, RuleJungle },
        { "Stutter Fills", "Clean bars, then the last beat of every second bar machine-guns.", { { Order, 0 } }, RuleStutterFills },
        { "Half-Time Drop", "Half speed and heavy, with a tape-stop into the loop point.", { { Speed, 0 }, { Order, 0 } }, RuleHalfTime },
        { "Reverse Ghosts", "Off-beat slices flip backwards and sit quieter.", { { Order, 0 }, { Swing, 0.15f } }, RuleReverseGhosts },
        { "Hip-Hop Skips", "Lazy swing, off-beats drop out now and then.", { { Swing, 0.45f }, { Order, 0 } }, RuleHipHopSkips },
        { "Tape Stop Ends", "Straight until the last slice, which winds down.", { { Order, 0 } }, RuleTapeStopEnds },
        { "Random Rain", "Any slice, any step, locked to seed 7.", { { Order, 1 }, { Lock, 1 }, { Seed, 7 }, { Mutation, 1.4f } }, RuleRain },
    };
    return list;
}

std::array<float, NumParams> presetValues (const PatternPreset& p, const std::array<float, NumParams>& current)
{
    std::array<float, NumParams> v {};
    const auto* d = paramDefs();
    for (int i = 0; i < NumParams; ++i) v[(size_t) i] = d[i].def;
    // keep how the break is sliced and mapped, and the FX capture settings
    for (int keep : { SliceMode, Sensitivity, MinSlice, RootNote, Polyphony, Choke, FollowHost, Stretch, Output, Mix, CaptureBars, Capture })
        v[(size_t) keep] = current[(size_t) keep];
    for (const auto& kv : p.values) v[(size_t) kv.first] = kv.second;
    return v;
}

void applyPresetRule (const PatternPreset& p, Kit& kit)
{
    if ((int) kit.settings.size() < kMaxSlices) kit.settings.resize ((size_t) kMaxSlices);
    const int n = (int) kit.slices.size();
    const double loop = kit.loopBeats();
    for (int i = 0; i < kMaxSlices; ++i)
    {
        auto& s = kit.settings[(size_t) i];
        s.prob = 1.0f; s.repeatProb = 0.0f; s.repeatRate = Rep32; s.reverseProb = 0.0f; s.dropProb = 0.0f; s.dropDepth = 0.6f;
        s.reverse = false; s.decay = 1.0f; s.level = 0.0f;
        if (i >= n) continue;
        const double b = kit.beatOf (kit.slices[(size_t) i].start);
        const double inBar = std::fmod (b, 4.0);
        const int bar = (int) (b / 4.0);
        const bool onBeat = std::fabs (inBar - std::round (inBar)) < 0.1;
        const bool lastSlice = i == n - 1 || (i + 1 < n && kit.beatOf (kit.slices[(size_t) i + 1].start) >= loop);
        switch (p.rule)
        {
            case RuleJungle:
                s.prob = 0.96f; s.repeatProb = 0.14f; s.repeatRate = (i % 3 == 0) ? Rep64 : Rep32; s.reverseProb = 0.07f;
                break;
            case RuleStutterFills:
                if (inBar >= 3.0 && bar % 2 == 1) { s.repeatProb = 0.85f; s.repeatRate = inBar >= 3.5 ? Rep64 : Rep32; }
                break;
            case RuleHalfTime:
                if (b >= loop - 1.0) { s.dropProb = 0.75f; s.dropDepth = 0.85f; }
                break;
            case RuleReverseGhosts:
                if (! onBeat) { s.reverseProb = 0.4f; s.level = -4.0f; s.prob = 0.9f; }
                break;
            case RuleHipHopSkips:
                if (! onBeat) s.prob = 0.65f;
                s.decay = 0.85f;
                break;
            case RuleTapeStopEnds:
                if (lastSlice) { s.dropProb = 0.95f; s.dropDepth = 1.0f; }
                break;
            case RuleRain:
                s.repeatProb = 0.18f; s.repeatRate = Rep16; s.reverseProb = 0.1f; s.prob = 0.9f;
                break;
            default: break;
        }
    }
}
} // namespace cs
