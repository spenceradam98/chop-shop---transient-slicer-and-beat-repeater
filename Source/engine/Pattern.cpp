#include "Pattern.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>

namespace cs
{
int stepCount (const Kit& k)
{
    if (k.empty()) return 0;
    int n = 0;
    for (const auto& s : k.slices)
        if (k.beatOf (s.start) < k.loopBeats() - 1e-6) ++n;
    return std::max (1, n);
}

double stepBeat (const Kit& k, int step, float swing)
{
    // the slice's own position keeps the break's groove; swing pushes the slices that sit
    // on an off-beat 16th later still
    double b = k.beatOf (k.slices[(size_t) step].start);
    if (swing > 0)
    {
        const double sixteenth = std::round (b * 4.0);
        if (std::fabs (b * 4.0 - sixteenth) < 0.2 && ((long long) sixteenth % 2) != 0) b += swing * 0.125;
    }
    return b;
}

StepPlan planStep (const Kit& k, const PatternSettings& ps, long long cycle, int step)
{
    StepPlan p;
    const int n = stepCount (k);
    if (n == 0) return p;
    const uint64_t seed = (uint64_t) ps.seed;
    const uint64_t cyc = ps.lock ? 0 : (uint64_t) cycle;
    const uint64_t st = (uint64_t) step;

    int slice = step;
    switch (ps.order)
    {
        case OrderRandom:
            slice = std::min (n - 1, (int) (hash01 (seed, cyc, st, 1) * n));
            break;
        case OrderReverse:
            slice = n - 1 - step;
            break;
        case OrderShuffle:
        {
            // permute the slices that start in the same bar as this step
            const int bar = (int) (k.beatOf (k.slices[(size_t) step].start) / 4.0);
            int members[kMaxSlices]; int m = 0, mine = 0;
            for (int i = 0; i < n; ++i)
                if ((int) (k.beatOf (k.slices[(size_t) i].start) / 4.0) == bar) { if (i == step) mine = m; members[m++] = i; }
            for (int i = m - 1; i > 0; --i)
            {
                const int j = (int) (hash01 (seed, cyc, (uint64_t) (bar * 131 + i), 6) * (i + 1));
                std::swap (members[i], members[std::min (i, j)]);
            }
            slice = members[mine];
            break;
        }
        default: break;
    }
    slice = std::clamp (slice, 0, n - 1);
    const auto& s = k.settingsFor (slice);
    const float mut = std::clamp (ps.mutation, 0.0f, 2.0f);
    auto prob = [&] (float x) { return std::clamp (x * mut, 0.0f, 1.0f); };

    p.slice = slice;
    p.play = hash01 (seed, cyc, st, 2) >= prob (1.0f - s.prob);
    p.repeat = hash01 (seed, cyc, st, 3) < prob (s.repeatProb);
    p.repeatRate = s.repeatRate;
    p.reverse = s.reverse != (hash01 (seed, cyc, st, 4) < prob (s.reverseProb));
    p.drop = hash01 (seed, cyc, st, 5) < prob (s.dropProb);
    p.dropDepth = s.dropDepth;
    p.gain = dbToGain (s.level);
    p.pitch = s.pitch;
    return p;
}
} // namespace cs
