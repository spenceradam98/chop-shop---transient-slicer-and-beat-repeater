#include "Breaks.h"
#include "Dsp.h"

#include <cmath>
#include <cstring>

namespace cs
{
namespace
{
// 2 bars of 16ths per line: X accent, x normal, g ghost, . rest
struct Groove
{
    BreakInfo info;
    const char* kick;
    const char* snare;
    const char* hat;
    const char* open;
    const char* rim;
    float swing;        // 0..1 of a 16th
    float kickTune, snareTune, tone;   // sound variation
};

const Groove grooves[] = {
    { { "Dusty Pocket", 92, 2 },
      "X.....x...x.....X.....x..x......",
      "....X..g.g..X..g....X..g.g..X.g.",
      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.xx",
      "..............x...............x.",
      "................................", 0.25f, 1.0f, 1.0f, 0.6f },
    { { "Rollout", 172, 2 },
      "X.........X.....X.x.......x.....",
      "....X..g.g..X.g.....X..g.g..X..g",
      "x.xxx.x.x.xxx.x.x.xxx.x.x.xxx.xx",
      "................................",
      "........x...........x.......x...", 0.0f, 1.1f, 1.15f, 0.8f },
    { { "Lazy Sunday", 84, 2 },
      "X......x..X.....X.......x.x.....",
      "....X.......X..g....X.......X...",
      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.",
      "......x...............x.........",
      "..g.......g.......g.......g.....", 0.45f, 0.9f, 0.9f, 0.4f },
    { { "Skip Step", 132, 2 },
      "X.....x...X.......x...X.....x...",
      "....X.......X.......X.......X...",
      "..x...x...x...x...x...x...x..xx.",
      "..x.......x.......x.......x.....",
      "...g.......g.......g.....g.g....", 0.55f, 1.2f, 1.05f, 0.7f },
    { { "Parade", 110, 2 },
      "X.......X.x.....X.......X.x...x.",
      "....X.gg....X.g.g...X.gg....XgXg",
      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.",
      "................................",
      "................................", 0.1f, 1.0f, 1.25f, 0.5f },
    { { "Half Brick", 140, 2 },
      "X.........x.....X.....x...x.....",
      "........X...............X.......",
      "x.xxx.x.x.x.x.xxx.x.x.x.x.xxx.x.",
      "..............x...............x.",
      "......g.........g.....g.....g...", 0.0f, 0.85f, 0.8f, 0.3f },
    { { "Ghost Notes", 100, 2 },
      "X.....x.x.......X..x..x.........",
      "g.g.X.ggg.g.Xg.gg.g.X.g.g.gX.g.g",
      "x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.x.",
      "..........................x.....",
      "................................", 0.3f, 1.05f, 0.95f, 0.55f },
    { { "Breakroom", 124, 2 },
      "X..x..x...X.....X..x..x.x.X.....",
      "....X.......X..g....X.......X.gX",
      "xxx.xxx.xxx.xxx.xxx.xxx.xxx.xxxx",
      "...x.......x.......x.......x....",
      "................................", 0.15f, 1.15f, 1.1f, 0.9f },
};
constexpr int kNumGrooves = (int) (sizeof (grooves) / sizeof (grooves[0]));

float velocityOf (char c) { return c == 'X' ? 1.0f : c == 'x' ? 0.72f : c == 'g' ? 0.28f : 0.0f; }

void addKick (std::vector<float>& l, std::vector<float>& r, int at, double fs, float vel, float tune, Rng& rng)
{
    const int len = (int) (0.45 * fs);
    double ph = 0;
    for (int i = 0; i < len && at + i < (int) l.size(); ++i)
    {
        const double t = i / fs;
        const double f = 48.0 * tune + 110.0 * tune * std::exp (-t * 28.0);
        ph += kTwoPi * f / fs;
        double s = std::sin (ph) * std::exp (-t * 7.5);
        if (i < (int) (0.004 * fs)) s += 0.5 * rng.bi() * (1.0 - i / (0.004 * fs));
        s = std::tanh (1.6 * s) * vel * 0.8;
        l[(size_t) (at + i)] += (float) s; r[(size_t) (at + i)] += (float) s;
    }
}

void addSnare (std::vector<float>& l, std::vector<float>& r, int at, double fs, float vel, float tune, float tone, Rng& rng)
{
    const int len = (int) (0.3 * fs);
    Svf bp; bp.set (fs, 2500.0 * tune + 2500.0 * tone, 0.8);
    double ph = 0;
    const float decay = vel < 0.4f ? 26.0f : 15.0f;
    for (int i = 0; i < len && at + i < (int) l.size(); ++i)
    {
        const double t = i / fs;
        ph += kTwoPi * (185.0 * tune) / fs;
        float hp;
        const float noise = rng.bi();
        bp.process (noise, hp);
        const double body = std::sin (ph) * std::exp (-t * 30.0);
        const double rattle = (0.6 * hp + 0.4 * noise) * std::exp (-t * decay);
        const double s = std::tanh (1.3 * (0.55 * body + 0.6 * rattle)) * vel * 0.7;
        l[(size_t) (at + i)] += (float) (s * 0.95); r[(size_t) (at + i)] += (float) (s * 1.05);
    }
}

void addHat (std::vector<float>& l, std::vector<float>& r, int at, double fs, float vel, bool open, float tone, Rng& rng)
{
    const int len = (int) ((open ? 0.35 : 0.06) * fs);
    Svf hpf; hpf.set (fs, 7000.0 + 2500.0 * tone, 0.7);
    // metallic: a few detuned squares mixed with noise
    const double freqs[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
    double ph[6] = {};
    for (int i = 0; i < len && at + i < (int) l.size(); ++i)
    {
        const double t = i / fs;
        double m = 0;
        for (int k = 0; k < 6; ++k) { ph[k] += freqs[k] * 2.0 / fs; m += std::fmod (ph[k], 1.0) < 0.5 ? 1.0 : -1.0; }
        float hp;
        hpf.process ((float) (0.12 * m + 0.7 * rng.bi()), hp);
        const double env = std::exp (-t * (open ? 9.0 : 60.0));
        const double s = hp * env * vel * 0.32;
        l[(size_t) (at + i)] += (float) (s * 1.1); r[(size_t) (at + i)] += (float) (s * 0.85);
    }
}

void addRim (std::vector<float>& l, std::vector<float>& r, int at, double fs, float vel, Rng& rng)
{
    const int len = (int) (0.05 * fs);
    double ph = 0;
    for (int i = 0; i < len && at + i < (int) l.size(); ++i)
    {
        const double t = i / fs;
        ph += kTwoPi * 820.0 / fs;
        const double s = (std::sin (ph) * 0.7 + 0.3 * rng.bi()) * std::exp (-t * 90.0) * vel * 0.5;
        l[(size_t) (at + i)] += (float) (s * 0.8); r[(size_t) (at + i)] += (float) (s * 1.2);
    }
}
} // namespace

const std::vector<BreakInfo>& factoryBreaks()
{
    static const std::vector<BreakInfo> list = []
    {
        std::vector<BreakInfo> v;
        for (const auto& g : grooves) v.push_back (g.info);
        return v;
    }();
    return list;
}

std::shared_ptr<Sample> renderBreak (int index, double fs)
{
    if (index < 0 || index >= kNumGrooves) return nullptr;
    const auto& g = grooves[index];
    const double beat = 60.0 / g.info.bpm;
    const int steps = g.info.bars * 16;
    const int total = (int) std::lround (g.info.bars * 4 * beat * fs);
    auto s = std::make_shared<Sample>();
    s->sampleRate = fs;
    s->name = g.info.name;
    s->l.assign ((size_t) total, 0.0f);
    s->r.assign ((size_t) total, 0.0f);
    Rng rng ((uint64_t) (index * 7919 + 13));
    for (int st = 0; st < steps; ++st)
    {
        double t = st * beat / 4.0;
        if (st % 2 == 1) t += g.swing * 0.5 * beat / 4.0;
        t += 0.0015 * rng.bi();                   // a little human timing
        const int at = std::max (0, (int) std::lround (t * fs));
        auto human = [&] (float v) { return v * (0.9f + 0.1f * rng.uni()); };
        if (float v = velocityOf (g.kick[st]))  addKick (s->l, s->r, at, fs, human (v), g.kickTune, rng);
        if (float v = velocityOf (g.snare[st])) addSnare (s->l, s->r, at, fs, human (v), g.snareTune, g.tone, rng);
        if (float v = velocityOf (g.open[st]))  addHat (s->l, s->r, at, fs, human (v), true, g.tone, rng);
        else if (float v2 = velocityOf (g.hat[st])) addHat (s->l, s->r, at, fs, human (v2) * (st % 2 ? 0.7f : 1.0f), false, g.tone, rng);
        if (float v = velocityOf (g.rim[st]))   addRim (s->l, s->r, at, fs, human (v), rng);
    }
    // gentle bus glue and a touch of tape-ish softness
    OnePoleLP lpL, lpR; lpL.set (fs, 9000.0 + 6000.0 * g.tone); lpR.set (fs, 9000.0 + 6000.0 * g.tone);
    float peak = 0;
    for (int i = 0; i < total; ++i)
    {
        s->l[(size_t) i] = std::tanh (1.2f * lpL.process (s->l[(size_t) i]));
        s->r[(size_t) i] = std::tanh (1.2f * lpR.process (s->r[(size_t) i]));
        peak = std::max (peak, std::max (std::fabs (s->l[(size_t) i]), std::fabs (s->r[(size_t) i])));
    }
    const float norm = peak > 0 ? 0.89f / peak : 1.0f;
    s->mono.resize ((size_t) total);
    for (int i = 0; i < total; ++i)
    {
        s->l[(size_t) i] *= norm; s->r[(size_t) i] *= norm;
        s->mono[(size_t) i] = 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]);
    }
    return s;
}
} // namespace cs
