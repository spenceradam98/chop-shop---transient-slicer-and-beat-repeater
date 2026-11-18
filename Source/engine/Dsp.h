// Small scalar DSP helpers (no SIMD intrinsics, no -march flags).
#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <vector>

namespace cs
{
constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }

// In-place iterative radix-2 FFT. n must be a power of two.
inline void fft (std::vector<std::complex<float>>& a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i)
    {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1)
    {
        const double ang = -kTwoPi / (double) len;
        const std::complex<float> wl ((float) std::cos (ang), (float) std::sin (ang));
        for (size_t i = 0; i < n; i += len)
        {
            std::complex<float> w (1.0f, 0.0f);
            for (size_t k = 0; k < len / 2; ++k)
            {
                const auto u = a[i + k], v = a[i + k + len / 2] * w;
                a[i + k] = u + v;
                a[i + k + len / 2] = u - v;
                w *= wl;
            }
        }
    }
}

// Stateless deterministic hash -> [0, 1). The pattern player uses it so that a given
// seed always makes the same decisions, independent of block size or playback history.
inline uint64_t mix64 (uint64_t x)
{
    x += 0x9E3779B97F4A7C15ull;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
    return x ^ (x >> 31);
}
inline double hash01 (uint64_t seed, uint64_t a, uint64_t b, uint64_t salt)
{
    const uint64_t h = mix64 (mix64 (mix64 (seed * 0x100000001B3ull + salt) ^ a) ^ (b * 0xD6E8FEB86659FD93ull));
    return (double) (h >> 11) * (1.0 / 9007199254740992.0);
}

// Small sequential RNG for synthesis (factory breaks).
struct Rng
{
    uint64_t s;
    explicit Rng (uint64_t seed = 1) : s (seed) {}
    uint64_t next() { s = mix64 (s); return s; }
    float uni() { return (float) ((next() >> 40) * (1.0 / 16777216.0)); }
    float bi() { return uni() * 2.0f - 1.0f; }
};

// TPT state-variable filter (Simper). Low-pass and high-pass outputs.
struct Svf
{
    float g = 0, k = 1.4f, ic1 = 0, ic2 = 0, a1 = 0, a2 = 0, a3 = 0;
    void set (double fs, double hz, double q = 0.707)
    {
        hz = std::clamp (hz, 10.0, fs * 0.45);
        g = (float) std::tan (kPi * hz / fs);
        k = (float) (1.0 / q);
        a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
    }
    void reset() { ic1 = ic2 = 0; }
    // returns low-pass, writes high-pass
    float process (float x, float& hp)
    {
        const float v3 = x - ic2;
        const float v1 = a1 * ic1 + a2 * v3;
        const float v2 = ic2 + a2 * ic1 + a3 * v3;
        ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
        hp = x - k * v1 - v2;
        return v2;
    }
};

struct OnePoleLP
{
    float a = 0, z = 0;
    void set (double fs, double hz) { a = (float) std::exp (-kTwoPi * hz / fs); }
    float process (float x) { z = x + a * (z - x); return z; }
};
} // namespace cs
