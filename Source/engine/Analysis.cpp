#include "Analysis.h"
#include "Dsp.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace cs
{
OnsetFunction computeOnsetFunction (const std::vector<float>& mono, double /*sampleRate*/)
{
    OnsetFunction of;
    const int n = of.fftSize, hop = of.hop, len = (int) mono.size();
    const int frames = len / hop + 1;
    std::vector<float> window ((size_t) n);
    for (int i = 0; i < n; ++i) window[(size_t) i] = (float) (0.5 - 0.5 * std::cos (kTwoPi * i / n));

    std::vector<std::complex<float>> buf ((size_t) n);
    std::vector<float> prev ((size_t) n / 2 + 1, 0.0f), mag ((size_t) n / 2 + 1);
    of.flux.assign ((size_t) frames, 0.0f);
    for (int t = 0; t < frames; ++t)
    {
        const int start = t * hop - n / 2;   // frame centred on t * hop
        for (int i = 0; i < n; ++i)
        {
            const int k = start + i;
            buf[(size_t) i] = { (k >= 0 && k < len) ? mono[(size_t) k] * window[(size_t) i] : 0.0f, 0.0f };
        }
        fft (buf);
        float f = 0;
        for (int b = 1; b <= n / 2; ++b)
        {
            mag[(size_t) b] = std::log1p (100.0f * std::abs (buf[(size_t) b]));
            f += std::max (0.0f, mag[(size_t) b] - prev[(size_t) b]);
        }
        of.flux[(size_t) t] = t == 0 ? 0.0f : f;
        std::swap (prev, mag);
    }
    const float peak = of.flux.empty() ? 0.0f : *std::max_element (of.flux.begin(), of.flux.end());
    if (peak > 0) for (auto& v : of.flux) v /= peak;
    return of;
}

int nudgeToZeroCrossing (const std::vector<float>& mono, int pos, int maxBack)
{
    const int len = (int) mono.size();
    if (pos <= 0 || pos >= len) return std::clamp (pos, 0, std::max (0, len - 1));
    for (int k = pos; k > std::max (0, pos - maxBack); --k)
        if (mono[(size_t) k - 1] == 0.0f || mono[(size_t) k] == 0.0f || (mono[(size_t) k - 1] < 0) != (mono[(size_t) k] < 0))
            return k;
    return pos;
}

// Find the sample where the hit near `centre` starts.
static int refineOnset (const std::vector<float>& x, double sr, int centre, int fftSize, int hop)
{
    const int len = (int) x.size();
    const int a = std::max (0, centre - fftSize / 2 - hop), b = std::min (len, centre + fftSize / 2 + hop);
    if (b <= a) return std::clamp (centre, 0, len);
    // Energy in short frames; the hit starts where energy jumps the most relative to just before it.
    const int w = 32;
    int best = a;
    double bestRatio = 0;
    for (int k = a; k + w <= b; k += 4)
    {
        double after = 0, before = 0;
        for (int i = 0; i < w; ++i) after += (double) x[(size_t) k + i] * x[(size_t) k + i];
        for (int i = 1; i <= 2 * w; ++i) { const int j = k - i; if (j >= 0) before += (double) x[(size_t) j] * x[(size_t) j]; }
        const double ratio = (after + 1e-12) / (before * 0.5 + 1e-9);
        if (ratio > bestRatio * 1.0001) { bestRatio = ratio; best = k; }
    }
    // inside the winning frame, the first sample above a fraction of the frame's peak
    float pk = 0;
    for (int i = 0; i < w && best + i < len; ++i) pk = std::max (pk, std::fabs (x[(size_t) best + i]));
    for (int i = 0; i < w && best + i < len; ++i)
        if (std::fabs (x[(size_t) best + i]) >= 0.2f * pk) { best += i; break; }
    return nudgeToZeroCrossing (x, best, (int) (0.001 * sr));
}

std::vector<int> detectTransients (const std::vector<float>& mono, double sr, const OnsetFunction& of, float sensitivity, float minSliceMs)
{
    std::vector<int> out { 0 };
    const auto& f = of.flux;
    const int frames = (int) f.size();
    const float delta = 0.02f + (1.0f - std::clamp (sensitivity, 0.0f, 1.0f)) * 0.3f;
    const int w = 8;
    const int minDist = (int) (minSliceMs * 0.001 * sr);
    std::vector<float> win;
    for (int t = 1; t < frames; ++t)
    {
        bool isPeak = true;
        for (int d = -2; d <= 2 && isPeak; ++d)
        {
            const int u = t + d;
            if (d != 0 && u >= 0 && u < frames && (f[(size_t) u] > f[(size_t) t] || (d < 0 && f[(size_t) u] == f[(size_t) t]))) isPeak = false;
        }
        if (! isPeak) continue;
        win.clear();
        for (int u = std::max (0, t - w); u <= std::min (frames - 1, t + w); ++u) win.push_back (f[(size_t) u]);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        const float thr = win[win.size() / 2] + delta;
        if (f[(size_t) t] < thr) continue;
        const int pos = refineOnset (mono, sr, t * of.hop, of.fftSize, of.hop);
        if (pos - out.back() < minDist) continue;
        out.push_back (pos);
    }
    return out;
}

std::vector<int> gridMarkers (const std::vector<float>& mono, int bars, int divisionsPerBar)
{
    std::vector<int> m;
    const int n = std::max (1, bars * divisionsPerBar);
    const double step = (double) mono.size() / n;
    for (int i = 0; i < n; ++i) m.push_back (i == 0 ? 0 : nudgeToZeroCrossing (mono, (int) std::lround (i * step), 32));
    return m;
}

double estimateTempo (const OnsetFunction& of, double sr, int lengthSamples, int& bars)
{
    const auto& f = of.flux;
    const double framesPerSec = sr / of.hop;
    const double durSec = lengthSamples / sr;
    if (f.size() < 16)
    {
        bars = 1;
        return std::clamp (240.0 / std::max (0.1, durSec), 60.0, 200.0);
    }

    // autocorrelation of the onset function (mean removed, normalised so ac[0] = 1)
    const double mean = std::accumulate (f.begin(), f.end(), 0.0) / (double) f.size();
    const int maxLag = (int) f.size() - 1;
    std::vector<double> ac ((size_t) maxLag + 1, 0.0);
    for (int lag = 0; lag <= maxLag; ++lag)
    {
        double s = 0;
        for (size_t i = (size_t) lag; i < f.size(); ++i) s += (f[i] - mean) * (f[i - (size_t) lag] - mean);
        ac[(size_t) lag] = s / (double) f.size();
    }
    const double a0 = ac[0] > 0 ? ac[0] : 1.0;
    for (auto& v : ac) v /= a0;
    auto acAt = [&] (double lag)
    {
        if (lag < 1 || lag >= maxLag) return 0.0;
        const int i = (int) lag; const double fr = lag - i;
        return ac[(size_t) i] * (1 - fr) + ac[(size_t) i + 1] * fr;
    };
    // salience of a tempo: periodicity at the half beat, beat, two beats and the bar
    auto salience = [&] (double bpm)
    {
        const double beat = framesPerSec * 60.0 / bpm;
        return 0.5 * acAt (beat * 0.5) + acAt (beat) + acAt (beat * 2.0) + acAt (beat * 4.0);
    };
    // a prior that prefers break-ish tempos (log-normal around 115 bpm)
    auto prior = [] (double bpm) { const double o = std::log2 (bpm / 115.0); return std::exp (-0.5 * o * o / (0.55 * 0.55)); };

    // Most breaks are trimmed to whole 4/4 bars: score each whole-bar tempo.
    double best = -1e30; int bestBars = 0;
    for (int b : { 1, 2, 4, 8, 16 })
    {
        const double bpm = 240.0 * b / durSec;
        if (bpm < 60.0 || bpm > 200.0) continue;
        const double score = (1.0 + salience (bpm)) * prior (bpm);
        if (score > best) { best = score; bestBars = b; }
    }
    if (bestBars > 0)
    {
        bars = bestBars;
        return 240.0 * bestBars / durSec;
    }

    // Not a whole number of bars: take the most salient tempo between 60 and 200 bpm.
    double bpmBest = 120.0; best = -1e30;
    for (double bpm = 60.0; bpm <= 200.0; bpm += 0.25)
    {
        const double score = (1.0 + salience (bpm)) * prior (bpm);
        if (score > best) { best = score; bpmBest = bpm; }
    }
    bars = std::max (1, (int) std::lround (durSec * bpmBest / 240.0));
    return bpmBest;
}

std::vector<Slice> slicesFromMarkers (std::vector<int> m, int length)
{
    std::vector<Slice> s;
    if (length <= 0) return s;
    m.push_back (0);
    std::sort (m.begin(), m.end());
    m.erase (std::unique (m.begin(), m.end()), m.end());
    m.erase (std::remove_if (m.begin(), m.end(), [length] (int v) { return v < 0 || v > length - 64; }), m.end());
    if (m.empty() || m.front() != 0) m.insert (m.begin(), 0);
    if ((int) m.size() > kMaxSlices) m.resize (kMaxSlices);
    for (size_t i = 0; i < m.size(); ++i) s.push_back ({ m[i], i + 1 < m.size() ? m[i + 1] : length });
    return s;
}

void sliceKit (Kit& kit, int mode, float sensitivity, float minSliceMs, const std::vector<int>& manualMarkers)
{
    if (kit.sample == nullptr) return;
    const auto& x = kit.sample->mono;
    std::vector<int> m;
    switch (mode)
    {
        case ModeGrid8:  m = gridMarkers (x, kit.bars, 8); break;
        case ModeGrid16: m = gridMarkers (x, kit.bars, 16); break;
        case ModeManual: m = manualMarkers; break;
        default: m = detectTransients (x, kit.sample->sampleRate, computeOnsetFunction (x, kit.sample->sampleRate), sensitivity, minSliceMs); break;
    }
    kit.slices = slicesFromMarkers (m, kit.sample->length());
    if ((int) kit.settings.size() < kMaxSlices) kit.settings.resize ((size_t) kMaxSlices);
}

Kit makeKit (std::shared_ptr<const Sample> s, int mode, float sensitivity, float minSliceMs, double bpm, int bars)
{
    Kit k;
    k.sample = std::move (s);
    k.settings.resize ((size_t) kMaxSlices);
    if (k.sample == nullptr || k.sample->length() == 0) return k;
    if (bpm > 0)
    {
        k.bpm = bpm;
        k.bars = bars > 0 ? bars : std::max (1, (int) std::lround (k.sample->length() / k.sample->sampleRate * bpm / 240.0));
    }
    else
    {
        const auto of = computeOnsetFunction (k.sample->mono, k.sample->sampleRate);
        k.bpm = estimateTempo (of, k.sample->sampleRate, k.sample->length(), k.bars);
    }
    sliceKit (k, mode, sensitivity, minSliceMs, {});
    return k;
}
} // namespace cs
