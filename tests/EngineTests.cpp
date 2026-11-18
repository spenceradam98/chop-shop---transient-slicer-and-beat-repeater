// Engine tests: parameter table, transient detection on a synthetic click track (every hit
// within 2 ms), tempo / bars detection on the factory breaks, seeded patterns repeating
// exactly, straight playback reproducing the break, capture in the FX build, every preset
// and stretch mode stable, and CPU headroom.
#include "Analysis.h"
#include "Breaks.h"
#include "Dsp.h"
#include "Engine.h"
#include "Presets.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <string>

using namespace cs;

static int failures = 0, checks = 0;
#define CHECK(cond, ...) do { ++checks; if (! (cond)) { ++failures; std::printf ("FAIL %s:%d: ", __FILE__, __LINE__); std::printf (__VA_ARGS__); std::printf ("\n"); } } while (0)

static std::shared_ptr<Sample> monoSample (std::vector<float> x, double fs)
{
    auto s = std::make_shared<Sample>();
    s->sampleRate = fs; s->l = x; s->r = x; s->mono = std::move (x);
    return s;
}

// Run the engine with a playing host transport. Returns the stereo output.
static void render (Engine& e, double fs, double bpm, double seconds, std::vector<float>& L, std::vector<float>& R,
                    int block = 256, const std::vector<float>* input = nullptr, double startPpq = 0)
{
    const int total = (int) (seconds * fs);
    L.assign ((size_t) total, 0.0f); R.assign ((size_t) total, 0.0f);
    std::vector<float> inL ((size_t) block, 0.0f);
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        if (input) for (int i = 0; i < n; ++i) inL[(size_t) i] = pos + i < (int) input->size() ? (*input)[(size_t) (pos + i)] : 0.0f;
        Transport tp; tp.hasInfo = true; tp.playing = true; tp.bpm = bpm; tp.ppq = startPpq + pos / fs * bpm / 60.0;
        e.process (input ? inL.data() : nullptr, input ? inL.data() : nullptr, L.data() + pos, R.data() + pos, n, tp, nullptr, 0);
    }
}

static bool allFinite (const std::vector<float>& x) { for (float v : x) if (! std::isfinite (v)) return false; return true; }
static double rms (const std::vector<float>& x, size_t a, size_t b) { double s = 0; for (size_t i = a; i < b && i < x.size(); ++i) s += (double) x[i] * x[i]; return std::sqrt (s / std::max<size_t> (1, b - a)); }

int main()
{
    const double fs = 48000.0;

    // ---- parameter table
    {
        std::set<std::string> ids;
        const auto* d = paramDefs();
        for (int i = 0; i < NumParams; ++i)
        {
            CHECK (ids.insert (d[i].id).second, "duplicate id %s", d[i].id);
            CHECK (d[i].def >= d[i].min && d[i].def <= d[i].max, "default out of range for %s", d[i].id);
            CHECK (paramIndex (d[i].id) == i, "paramIndex %s", d[i].id);
        }
    }

    // ---- transient detection: synthetic click track, irregular spacing and levels
    {
        Rng rng (5);
        std::vector<int> truth;
        int t = (int) (0.05 * fs);
        for (int k = 0; k < 24; ++k) { truth.push_back (t); t += (int) ((0.09 + 0.3 * rng.uni()) * fs); }
        std::vector<float> x ((size_t) (t + fs * 0.3), 0.0f);
        for (auto& v : x) v = 0.002f * rng.bi();                           // noise floor
        for (size_t k = 0; k < truth.size(); ++k)
        {
            const float amp = 0.25f + 0.7f * rng.uni();
            const double f = 600.0 + 3000.0 * rng.uni();
            for (int i = 0; i < (int) (0.06 * fs); ++i)
                x[(size_t) (truth[k] + i)] += (float) (amp * std::sin (kTwoPi * f * i / fs) * std::exp (-i / (0.008 * fs)));
        }
        const auto of = computeOnsetFunction (x, fs);
        auto found = detectTransients (x, fs, of, 0.6f, 40.0f);
        found.erase (found.begin());  // the forced marker at 0
        CHECK (found.size() == truth.size(), "click track: found %d hits, expected %d", (int) found.size(), (int) truth.size());
        if (std::getenv ("CS_VERBOSE")) { for (int tr : truth) std::printf ("t %.1f ", tr / fs * 1000.0); std::printf ("\n"); for (int f : found) std::printf ("f %.1f ", f / fs * 1000.0); std::printf ("\n"); }
        double worst = 0;
        for (int tr : truth)
        {
            double best = 1e9;
            for (int f : found) best = std::min (best, std::fabs ((double) f - tr));
            worst = std::max (worst, best);
            if (best > 0.002 * fs) std::printf ("  missed click at %.1f ms\n", tr / fs * 1000.0);
        }
        CHECK (worst <= 0.002 * fs, "click track: worst onset error %.2f ms", worst / fs * 1000.0);
        std::printf ("click track: %d/%d hits, worst error %.2f ms\n", (int) found.size(), (int) truth.size(), worst / fs * 1000.0);

        // sensitivity: lower finds fewer or equal
        const auto fewer = detectTransients (x, fs, of, 0.0f, 40.0f);
        CHECK (fewer.size() <= found.size() + 1, "lower sensitivity found more");
        // min slice length is respected
        const auto spaced = detectTransients (x, fs, of, 1.0f, 200.0f);
        for (size_t i = 1; i < spaced.size(); ++i) CHECK (spaced[i] - spaced[i - 1] >= (int) (0.2 * fs), "min slice violated");
    }

    // ---- factory breaks: tempo, bars and slicing
    int tempoExact = 0;
    for (int b = 0; b < (int) factoryBreaks().size(); ++b)
    {
        const auto& info = factoryBreaks()[(size_t) b];
        auto s = renderBreak (b, fs);
        CHECK (s && allFinite (s->l) && allFinite (s->r), "break %s not finite", info.name);
        const Kit k = makeKit (s, ModeTransient, 0.6f, 40.0f);
        // a drum-and-bass break can honestly read as half time (the tempo stays editable), so an
        // octave either way passes, as long as tempo and bar count describe the same loop
        const double ratio = k.bpm / info.bpm;
        const bool octave = std::fabs (ratio - 1) < 0.01 || std::fabs (ratio - 0.5) < 0.005 || std::fabs (ratio - 2) < 0.02;
        CHECK (octave, "break %s tempo %.2f, expected %.0f", info.name, k.bpm, info.bpm);
        CHECK (std::fabs (k.bars / (double) info.bars - ratio) < 0.02, "break %s bars %d", info.name, k.bars);
        tempoExact += std::fabs (ratio - 1) < 0.01 ? 1 : 0;
        CHECK (k.slices.size() >= 8 && k.slices.size() <= (size_t) kMaxSlices, "break %s: %d slices", info.name, (int) k.slices.size());
        for (size_t i = 0; i < k.slices.size(); ++i)
            CHECK (k.slices[i].end > k.slices[i].start && (i == 0 || k.slices[i].start == k.slices[i - 1].end), "slices not contiguous");
        std::printf ("break %-14s %6.2f bpm, %d bars, %2d slices\n", info.name, k.bpm, k.bars, (int) k.slices.size());

        Kit g = k; sliceKit (g, ModeGrid16, 0.6f, 40.0f, {});
        CHECK ((int) g.slices.size() == k.bars * 16, "grid 1/16 slices %d", (int) g.slices.size());
        sliceKit (g, ModeGrid8, 0.6f, 40.0f, {});
        CHECK ((int) g.slices.size() == k.bars * 8, "grid 1/8 slices %d", (int) g.slices.size());
        sliceKit (g, ModeManual, 0.6f, 40.0f, { 0, 1000, 5000, 5000, 999999999 });
        CHECK (g.slices.size() == 3, "manual markers -> %d slices", (int) g.slices.size());
    }

    CHECK (tempoExact >= (int) factoryBreaks().size() - 1, "only %d breaks got the exact tempo", tempoExact);

    // ---- seeded patterns repeat exactly
    {
        Kit k = makeKit (renderBreak (1, fs), ModeTransient, 0.6f, 40.0f);
        applyPresetRule (patternPresets()[1], k);   // Jungle Mutate: plenty of randomness
        PatternSettings ps; ps.order = OrderShuffle; ps.mutation = 1.5f; ps.seed = 42;
        auto sig = [&] (const PatternSettings& p, long long cycle)
        {
            std::string s;
            for (int i = 0; i < stepCount (k); ++i)
            {
                const auto st = planStep (k, p, cycle, i);
                s += std::to_string (st.play) + ":" + std::to_string (st.slice) + ":" + std::to_string (st.repeat) + std::to_string (st.reverse) + std::to_string (st.drop) + ",";
            }
            return s;
        };
        CHECK (sig (ps, 3) == sig (ps, 3), "same seed + cycle differs");
        CHECK (sig (ps, 3) != sig (ps, 4), "unlocked cycles identical");
        auto other = ps; other.seed = 43;
        CHECK (sig (ps, 3) != sig (other, 3), "different seeds identical");
        auto locked = ps; locked.lock = true;
        CHECK (sig (locked, 0) == sig (locked, 17), "locked cycles differ");
        auto still = ps; still.mutation = 0; still.order = OrderOriginal;
        bool straight = true;
        for (int i = 0; i < stepCount (k); ++i) { const auto st = planStep (k, still, 9, i); straight &= st.play && st.slice == i && ! st.repeat && ! st.drop; }
        CHECK (straight, "mutation 0 is not straight playback");
        // shuffle in bar keeps every slice in its bar and uses each once
        std::set<int> used;
        for (int i = 0; i < stepCount (k); ++i)
        {
            const auto st = planStep (k, ps, 2, i);
            used.insert (st.slice);
            CHECK ((int) (k.beatOf (k.slices[(size_t) st.slice].start) / 4) == (int) (k.beatOf (k.slices[(size_t) i].start) / 4), "shuffle left its bar");
        }
        CHECK ((int) used.size() == stepCount (k), "shuffle repeated a slice");

        // the engine renders the same seed bit-identically
        std::vector<float> a, b, c, d;
        for (int rep = 0; rep < 2; ++rep)
        {
            Engine e; e.prepare (fs, 256, false);
            e.setParam (Order, OrderShuffle); e.setParam (Mutation, 1.5f); e.setParam (Seed, 42);
            e.submitKit (std::make_unique<Kit> (k), false);
            render (e, fs, 172.0, 6.0, rep ? c : a, rep ? d : b);
        }
        CHECK (a == c && b == d, "seeded render not repeatable");
        CHECK (rms (a, 0, a.size()) > 0.02, "seeded render silent");
    }

    // ---- straight chop at the original tempo reproduces the break
    {
        auto s = renderBreak (0, fs);
        Kit k = makeKit (s, ModeTransient, 0.6f, 40.0f);
        for (int mode = 0; mode < 3; ++mode)
        {
            Engine e; e.prepare (fs, 512, false);
            e.setParam (Stretch, (float) mode);
            e.submitKit (std::make_unique<Kit> (k), false);
            std::vector<float> L, R;
            const double loopSec = k.bars * 4 * 60.0 / k.bpm;
            render (e, fs, k.bpm, loopSec * 2.0, L, R, 512);
            // compare the second loop with the source
            const size_t off = (size_t) std::lround (loopSec * fs), len = s->l.size();
            double num = 0, da = 0, db = 0;
            for (size_t i = 0; i < len && off + i < L.size(); ++i) { num += (double) L[off + i] * s->l[i]; da += (double) L[off + i] * L[off + i]; db += (double) s->l[i] * s->l[i]; }
            const double corr = num / std::sqrt (da * db + 1e-12);
            CHECK (corr > (mode == 2 ? 0.8 : 0.97), "straight chop (stretch mode %d) correlation %.3f", mode, corr);
            std::printf ("straight chop, stretch mode %d: correlation with source %.4f\n", mode, corr);
        }
    }

    // ---- slower host: Off leaves gaps after each slice, Repitch and Stretch fill the step
    {
        Kit k = makeKit (renderBreak (4, fs), ModeTransient, 0.6f, 40.0f);
        double gaps[3] = {};
        for (int mode = 0; mode < 3; ++mode)
        {
            Engine e; e.prepare (fs, 256, false);
            e.setParam (Stretch, (float) mode);
            e.submitKit (std::make_unique<Kit> (k), false);
            std::vector<float> L, R;
            render (e, fs, k.bpm * 0.7, 6.0, L, R);
            const size_t w = (size_t) (0.01 * fs);
            int quiet = 0, total = 0;
            for (size_t a = 0; a + w < L.size(); a += w, ++total) quiet += rms (L, a, a + w) < 0.002 ? 1 : 0;
            gaps[mode] = quiet / (double) total;
        }
        std::printf ("slower host, silent 10 ms windows: off %.1f%%, repitch %.1f%%, stretch %.1f%%\n", gaps[0] * 100, gaps[1] * 100, gaps[2] * 100);
        CHECK (gaps[1] < gaps[0] && gaps[2] < gaps[0], "repitch / stretch did not fill the gaps");
    }

    // ---- every preset and stretch mode at a different host tempo: stable, audible
    for (size_t p = 0; p < patternPresets().size(); ++p)
        for (int mode = 0; mode < 3; ++mode)
        {
            const auto& pr = patternPresets()[p];
            Kit k = makeKit (renderBreak ((int) p % (int) factoryBreaks().size(), fs), ModeTransient, 0.6f, 40.0f);
            applyPresetRule (pr, k);
            Engine e; e.prepare (fs, 256, false);
            std::array<float, NumParams> cur {};
            for (int i = 0; i < NumParams; ++i) cur[(size_t) i] = e.param (i);
            const auto v = presetValues (pr, cur);
            for (int i = 0; i < NumParams; ++i) e.setParam (i, v[(size_t) i]);
            e.setParam (Stretch, (float) mode);
            e.submitKit (std::make_unique<Kit> (k), false);
            std::vector<float> L, R;
            render (e, fs, k.bpm * 1.23, 5.0, L, R);
            float peak = 0; for (float x : L) peak = std::max (peak, std::fabs (x));
            CHECK (allFinite (L) && allFinite (R), "preset %s mode %d not finite", pr.name, mode);
            CHECK (rms (L, 0, L.size()) > 0.01 && peak < 4.0f, "preset %s mode %d level rms %.4f peak %.2f", pr.name, mode, rms (L, 0, L.size()), peak);
        }

    // ---- MIDI: root note + i plays slice i; filter and crush keep it finite
    {
        Kit k = makeKit (renderBreak (2, fs), ModeTransient, 0.6f, 40.0f);
        Engine e; e.prepare (fs, 256, false);
        e.setParam (PatOn, 0);
        e.setParam (Filter, -0.6f); e.setParam (Crush, 0.5f);
        e.submitKit (std::make_unique<Kit> (k), false);
        std::vector<float> L (256), R (256);
        Transport tp;
        e.process (nullptr, nullptr, L.data(), R.data(), 256, tp, nullptr, 0);
        CHECK (rms (L, 0, 256) == 0.0, "pattern off but output not silent");
        NoteEvent ev { 10, 36 + 2, 1.0f };
        double s = 0;
        for (int b = 0; b < 40; ++b) { e.process (nullptr, nullptr, L.data(), R.data(), 256, tp, b == 0 ? &ev : nullptr, b == 0 ? 1 : 0); s += rms (L, 0, 256); }
        CHECK (s > 0.05, "MIDI note did not play a slice");
        CHECK (e.hits[2].load() == 1, "hit counter for slice 2 = %u", e.hits[2].load());
        NoteEvent outOfRange { 0, 10, 1.0f };
        e.process (nullptr, nullptr, L.data(), R.data(), 256, tp, &outOfRange, 1);
        CHECK (allFinite (L), "out-of-range note");
    }

    // ---- FX build: capture N bars from the downbeat, then slice it
    {
        auto src = renderBreak (3, fs);   // 132 bpm, 2 bars
        std::vector<float> input = src->l;
        input.insert (input.end(), src->l.begin(), src->l.end());
        input.insert (input.end(), src->l.begin(), src->l.end());
        Engine e; e.prepare (fs, 256, true);
        e.setParam (CaptureBars, 1);   // 2 bars
        e.armCapture();
        std::vector<float> L, R;
        // start mid-bar (beat 1.5) so the capture has to wait for the next downbeat at beat 4
        render (e, fs, 132.0, 6.0, L, R, 256, &input, 1.5);
        CHECK (e.captureReady(), "capture not ready");
        std::vector<float> cl, cr; double bpm = 0; int bars = 0;
        CHECK (e.takeCapture (cl, cr, bpm, bars), "takeCapture failed");
        const int expect = (int) std::lround (2 * 4 * 60.0 / 132.0 * fs);
        CHECK ((int) cl.size() == expect && bars == 2 && std::fabs (bpm - 132.0) < 1e-6, "capture %d samples, %d bars", (int) cl.size(), bars);
        // dry signal passes while nothing is captured
        CHECK (rms (L, 0, 4096) > 0.01, "FX did not pass the dry signal before capture");
        // the capture begins at the downbeat (beat 4 = 2.5 beats after the start)
        const int startAt = (int) std::lround (2.5 * 60.0 / 132.0 * fs);
        double err = 0; for (int i = 0; i < 2000; ++i) err = std::max (err, (double) std::fabs (cl[(size_t) i] - input[(size_t) (startAt + i)]));
        CHECK (err < 0.02, "capture not aligned to the downbeat (err %.3f)", err);
        // build a kit from it and hand it over at the next downbeat
        auto s = monoSample (cl, fs); s->r = cr;
        for (size_t i = 0; i < cl.size(); ++i) s->mono[i] = 0.5f * (cl[i] + cr[i]);
        Kit k = makeKit (s, ModeTransient, 0.6f, 40.0f, bpm, bars);
        CHECK (k.slices.size() >= 8, "captured kit has %d slices", (int) k.slices.size());
        e.submitKit (std::make_unique<Kit> (k), true);
        std::vector<float> L2, R2;
        render (e, fs, 132.0, 5.0, L2, R2, 256, &input, 12.0);
        CHECK (! e.hasPendingKit(), "pending kit never swapped in");
        CHECK (allFinite (L2) && rms (L2, 0, L2.size()) > 0.02, "FX output after capture");
        e.collectGarbage();
    }

    // ---- kit hand-over while playing does not crash and frees the old kit
    {
        Engine e; e.prepare (fs, 128, false);
        std::vector<float> L, R;
        for (int b = 0; b < 6; ++b)
        {
            e.submitKit (std::make_unique<Kit> (makeKit (renderBreak (b, fs), ModeTransient, 0.6f, 40.0f)), b % 2 == 0);
            render (e, fs, 120.0, 1.0, L, R, 128, nullptr, b * 2.0);
            e.collectGarbage();
            CHECK (allFinite (L), "swap %d", b);
        }
    }

    // ---- CPU: stretch mode with stutters, 20 s of audio
    {
        Kit k = makeKit (renderBreak (1, fs), ModeTransient, 0.6f, 40.0f);
        applyPresetRule (patternPresets()[1], k);
        Engine e; e.prepare (fs, 256, false);
        e.setParam (Stretch, 2); e.setParam (Order, OrderShuffle);
        e.submitKit (std::make_unique<Kit> (k), false);
        std::vector<float> L, R;
        const auto t0 = std::chrono::steady_clock::now();
        render (e, fs, 150.0, 20.0, L, R);
        const double sec = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
        std::printf ("CPU: 20 s of stretch-mode playback in %.3f s (%.1f%% of real time)\n", sec, sec / 20.0 * 100.0);
        CHECK (sec < 2.0, "too slow: %.2f s", sec);
    }

    std::printf ("%d checks, %d failures\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
