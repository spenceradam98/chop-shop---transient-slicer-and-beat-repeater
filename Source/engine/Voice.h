// One slice player. Plays a slice forwards or backwards, repitched (rate) or time-stretched
// (WSOLA: windowed grains whose start is chosen by waveform similarity), with short fades at
// every edge so slices never click. Also does the per-step tricks: stutter repeats and a
// tape-stop style speed drop.
#pragma once

#include "Kit.h"
#include "Dsp.h"

namespace cs
{
struct VoiceStart
{
    int sliceIndex = 0;
    int start = 0, end = 0;          // slice region in the source
    float gain = 1.0f;
    double rate = 1.0;               // playback speed (repitch) or, when stretching, speed through the source
    double pitch = 1.0;              // pitch ratio inside grains (stretch mode only)
    bool stretch = false;
    bool reverse = false;
    double maxLength = 0;            // output samples before the voice is cut (0 = until the slice ends)
    double repeatInterval = 0;       // output samples between stutter retriggers (0 = no stutter)
    bool drop = false;
    float dropDepth = 0.6f;
    float attackMs = 0, decay = 1.0f;
};

class Voice
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        hop = std::max (64, (int) (0.011 * sr));
        grainLen = 2 * hop;
        edgeFade = std::max (8, (int) (0.0015 * sr));
        headFade = std::max (4, (int) (0.0002 * sr));   // slices start on a zero crossing: keep the attack
        releaseLen = std::max (16, (int) (0.003 * sr));
        reset();
    }
    void reset() { active = false; releasing = false; }
    bool isActive() const { return active; }
    int slice() const { return v.sliceIndex; }
    const Sample* source() const { return src; }
    double sourcePosition() const   // for the UI play cursor
    {
        const double p = v.stretch ? g[cur].pos : pos;
        return v.reverse ? v.end - 1 - p : v.start + p;
    }

    void start (const Sample* s, const VoiceStart& vs)
    {
        src = s; v = vs;
        len = std::max (1, v.end - v.start);
        active = src != nullptr && len > 1;
        releasing = false; relCount = 0;
        elapsed = 0; segElapsed = 0; pos = 0; nominal = 0;
        expectedSeg = len / std::max (1e-6, v.rate);
        if (v.repeatInterval > 0) expectedSeg = std::min (expectedSeg, v.repeatInterval);
        refLen = v.maxLength > 0 ? v.maxLength : len / std::max (1e-6, v.rate);
        attackSamples = v.attackMs * 0.001 * sr;
        // stretching where speed and pitch agree is plain playback at that rate
        if (v.stretch && std::fabs (v.rate - v.pitch) < 1e-4 * v.rate) v.stretch = false;
        g[0] = { 0.0, 0, true }; g[1] = { 0.0, 0, false }; cur = 0; sinceSpawn = 0;
    }

    void release() { if (active && ! releasing) { releasing = true; relCount = 0; } }

    // Adds into out (stereo).
    void render (float* outL, float* outR, int n)
    {
        for (int i = 0; i < n && active; ++i)
        {
            if (v.maxLength > 0 && elapsed >= v.maxLength - releaseLen) release();

            // stutter: jump back to the slice head
            if (v.repeatInterval > 0 && segElapsed >= v.repeatInterval)
            {
                segElapsed = 0; pos = 0; nominal = 0;
                g[0] = { 0.0, 0, true }; g[1] = { 0.0, 0, false }; cur = 0; sinceSpawn = 0;
            }

            double speed = v.rate, pr = v.pitch;
            if (v.drop)
            {
                const double ramp = std::max (0.06, 1.0 - v.dropDepth * 0.94 * std::min (1.0, elapsed / refLen));
                speed *= ramp; pr *= ramp;
            }

            float l = 0, r = 0;
            bool ended = false;
            if (v.stretch) ended = stepStretch (speed, pr, l, r);
            else
            {
                if (pos >= len) ended = true;
                else { read (pos, l, r); pos += speed; }
            }
            if (ended) { active = false; break; }

            // envelopes
            float env = v.gain;
            const double segPos = v.stretch ? nominal : pos;
            const int inFade = (segElapsed > 0 && v.repeatInterval > 0) || v.reverse ? edgeFade : headFade;
            if (segElapsed < inFade) env *= (float) (segElapsed + 1) / (float) inFade;
            const double left = (len - segPos) / std::max (1e-6, speed);
            if (left < edgeFade) env *= (float) std::max (0.0, left / edgeFade);
            if (v.repeatInterval > 0)
            {
                const double toNext = v.repeatInterval - segElapsed;
                if (toNext < edgeFade) env *= (float) std::max (0.0, toNext / edgeFade);
            }
            if (attackSamples > 1 && segElapsed < attackSamples) env *= (float) (segElapsed / attackSamples);
            if (v.decay < 0.999f)
            {
                const double p = segElapsed / std::max (1.0, expectedSeg * v.decay);
                const double d = std::max (0.0, 1.0 - p);
                env *= (float) (d * std::sqrt (d));
            }
            if (releasing)
            {
                env *= 1.0f - (float) relCount / (float) releaseLen;
                if (++relCount >= releaseLen) active = false;
            }
            outL[i] += l * env;
            outR[i] += r * env;
            ++elapsed; ++segElapsed;
        }
    }

private:
    // Read the slice-local position p (fractional) with linear interpolation; silent outside the slice.
    void read (double p, float& l, float& r) const
    {
        if (p < 0 || p >= len - 1) { l = r = 0; return; }
        const int i = (int) p;
        const float f = (float) (p - i);
        int a = v.start + i, b = a + 1;
        if (v.reverse) { a = v.end - 1 - i; b = a - 1; }
        l = src->l[(size_t) a] + f * (src->l[(size_t) b] - src->l[(size_t) a]);
        r = src->r[(size_t) a] + f * (src->r[(size_t) b] - src->r[(size_t) a]);
    }
    float readMono (double p) const
    {
        if (p < 0 || p >= len - 1) return 0.0f;
        const int i = (int) p;
        return src->mono[(size_t) (v.reverse ? v.end - 1 - i : v.start + i)];
    }

    struct Grain { double pos; int age; bool on; };

    // WSOLA: grains of 2*hop samples, Hann windowed, a new one every hop samples. The new grain
    // starts near where the source "should" be (nominal), at the offset whose waveform best
    // matches the natural continuation of the previous grain.
    bool stepStretch (double speed, double pr, float& l, float& r)
    {
        if (sinceSpawn >= hop || ! g[cur].on)
        {
            const Grain& prev = g[cur];
            const double nat = prev.pos + hop * pr;
            double best = nominal, bestScore = -1e30;
            if (prev.on && segElapsed > 0)
            {
                const int tol = hop / 2;
                for (int d = -tol; d <= tol; d += 4)
                {
                    const double cand = nominal + d;
                    if (cand < 0) continue;
                    double s = 0, e = 1e-9;
                    for (int j = 0; j < hop; j += 4)
                    {
                        const float c = readMono (cand + j * pr);
                        s += readMono (nat + j * pr) * c; e += c * c;
                    }
                    s = s / std::sqrt (e) - 1e-4 * std::abs (d);   // prefer staying on course when it is a tie
                    if (s > bestScore) { bestScore = s; best = cand; }
                }
            }
            const int next = prev.on && segElapsed > 0 ? 1 - cur : cur;
            g[next] = { std::max (0.0, best), 0, true };
            cur = next;
            sinceSpawn = 0;
        }
        if (nominal >= len + grainLen * pr) return true;
        for (auto& gr : g)
        {
            if (! gr.on) continue;
            const float w = (float) (0.5 - 0.5 * std::cos (kTwoPi * gr.age / grainLen));
            float a, b;
            read (gr.pos + gr.age * pr, a, b);
            l += a * w; r += b * w;
            if (++gr.age >= grainLen) gr.on = false;
        }
        nominal += speed;
        ++sinceSpawn;
        return false;
    }

    double sr = 44100;
    int hop = 512, grainLen = 1024, edgeFade = 64, headFade = 8, releaseLen = 128;
    const Sample* src = nullptr;
    VoiceStart v;
    int len = 1;
    bool active = false, releasing = false;
    int relCount = 0;
    double elapsed = 0, segElapsed = 0, pos = 0, nominal = 0, expectedSeg = 1, refLen = 1, attackSamples = 0;
    Grain g[2] {};
    int cur = 0, sinceSpawn = 0;
};
} // namespace cs
