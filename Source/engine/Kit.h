// A Kit is one loaded (or captured) break: the audio, the slice markers, the per-slice
// settings and the original tempo. Kits are immutable once handed to the audio thread;
// the UI edits a copy and submits it (see Engine::submitKit).
#pragma once

#include <memory>
#include <string>
#include <vector>

namespace cs
{
constexpr int kMaxSlices = 64;

struct Sample
{
    std::vector<float> l, r, mono;
    double sampleRate = 44100.0;
    std::string name;
    int length() const { return (int) mono.size(); }
};

enum RepeatRate { Rep16 = 0, Rep32, Rep64, NumRepeatRates };
inline const char* repeatRateName (int r) { static const char* n[] = { "1/16", "1/32", "1/64" }; return n[r < 0 || r > 2 ? 0 : r]; }

struct SliceSettings
{
    float level = 0.0f;        // dB
    float pitch = 0.0f;        // semitones
    bool reverse = false;
    float attack = 0.0f;       // ms
    float decay = 1.0f;        // fraction of the slice before it has faded out (1 = full length)
    float prob = 1.0f;         // chance the step plays at all
    float repeatProb = 0.0f;   // chance the step stutters
    int repeatRate = Rep32;
    float reverseProb = 0.0f;  // chance the step plays backwards
    float dropProb = 0.0f;     // chance of a tape-stop style pitch drop across the step
    float dropDepth = 0.6f;    // 0..1, how far the speed falls

    bool operator== (const SliceSettings& o) const
    {
        auto eq = [] (float x, float y) { return ! (x < y) && ! (y < x); };
        return eq (level, o.level) && eq (pitch, o.pitch) && reverse == o.reverse && eq (attack, o.attack) && eq (decay, o.decay)
            && eq (prob, o.prob) && eq (repeatProb, o.repeatProb) && repeatRate == o.repeatRate && eq (reverseProb, o.reverseProb)
            && eq (dropProb, o.dropProb) && eq (dropDepth, o.dropDepth);
    }
};

struct Slice
{
    int start = 0, end = 0;    // samples in the source
    int length() const { return end - start; }
};

struct Kit
{
    std::shared_ptr<const Sample> sample;
    std::vector<Slice> slices;
    std::vector<SliceSettings> settings;   // same size as slices (kept up to kMaxSlices so re-slicing keeps them)
    double bpm = 120.0;                    // original tempo of the break
    int bars = 1;                          // loop length in 4/4 bars
    bool tempoFromUser = false;

    bool empty() const { return sample == nullptr || slices.empty(); }
    double loopBeats() const { return bars * 4.0; }
    double beatOf (double samplePos) const { return sample ? samplePos / sample->sampleRate * bpm / 60.0 : 0.0; }
    const SliceSettings& settingsFor (int i) const { static SliceSettings d; return i >= 0 && i < (int) settings.size() ? settings[(size_t) i] : d; }
};
} // namespace cs
