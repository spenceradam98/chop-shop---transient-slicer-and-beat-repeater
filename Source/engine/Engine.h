// The chop-shop engine: no JUCE, unit tested. Owns the slice voices, the pattern player,
// the FX-build capture buffer and the global effects (filter, crush, output).
//
// Threading: the audio thread calls process(). The message thread builds Kits and hands
// them over with submitKit(); the audio thread swaps them in (immediately, or on the next
// loop downbeat) and hands the old one back through a lock-free queue that the message
// thread empties with collectGarbage(). Nothing on the audio thread allocates or frees.
#pragma once

#include "Kit.h"
#include "Params.h"
#include "Pattern.h"
#include "Voice.h"

#include <array>
#include <atomic>
#include <memory>

namespace cs
{
struct Transport
{
    bool hasInfo = false;   // the host gave us a position at all
    bool playing = false;
    double ppq = 0;         // quarter notes at the start of the block
    double bpm = 120;
    int barBeats = 4;       // quarter notes per bar
};

struct NoteEvent { int offset; int note; float velocity; };   // velocity 0 = note off

class Engine
{
public:
    Engine();
    ~Engine();

    void prepare (double sampleRate, int maxBlock, bool fxBuild);
    void setParam (int index, float value) { params[(size_t) index] = value; }
    float param (int index) const { return params[(size_t) index]; }

    // ---- message thread
    void submitKit (std::unique_ptr<Kit>, bool atDownbeat);
    void collectGarbage();
    bool hasPendingKit() const { return pending.load() != nullptr; }

    // FX build: capture
    void armCapture() { captureArmed.store (true); }
    bool captureReady() const { return captureState.load() == CapReady; }
    // Copies the captured audio out and frees the capture slot. Returns false if nothing is ready.
    bool takeCapture (std::vector<float>& l, std::vector<float>& r, double& bpm, int& bars);

    // ---- audio thread
    void process (const float* inL, const float* inR, float* outL, float* outR, int n,
                  const Transport&, const NoteEvent* events, int numEvents);

    // ---- read by the UI
    std::atomic<int> playingSlice { -1 };       // slice of the pattern voice, -1 = none
    std::atomic<float> playCursor { -1.0f };    // source position of the pattern voice (samples)
    std::array<std::atomic<uint32_t>, kMaxSlices> hits {};   // trigger counters per slice
    std::atomic<float> outLevel { 0.0f };
    std::atomic<float> captureProgress { 0.0f }; // 0..1 while recording
    std::atomic<int> captureStatus { 0 };        // 0 idle, 1 waiting for downbeat, 2 recording, 3 ready

    static constexpr int kMidiVoices = 16;

private:
    enum CapState { CapIdle = 0, CapWaiting, CapRecording, CapReady };

    void swapKit (Kit*);
    void renderSegment (float* outL, float* outR, int from, int to);
    void triggerStep (long long cycle, int step, int offsetInBlock, double stepLenSamples, double hostSamplesPerBeat);
    void noteOn (int note, float velocity, double hostBpm);
    double stretchRatio (double hostBpm) const;   // source speed for the current stretch mode
    void handleCapture (const float* inL, const float* inR, int n, const Transport&);
    void applyGlobal (float* outL, float* outR, const float* inL, const float* inR, int n);

    double sr = 44100;
    bool fx = false;
    std::array<float, NumParams> params {};

    Kit* kit = nullptr;                    // audio thread's kit
    std::atomic<Kit*> pending { nullptr };
    std::atomic<bool> pendingAtDownbeat { false };
    Kit* retiring = nullptr;               // old kit whose sample voices are fading out
    static constexpr int kRetired = 32;
    std::array<std::atomic<Kit*>, kRetired> retired {};

    std::array<Voice, kMidiVoices> midiVoices;
    std::array<uint32_t, kMidiVoices> voiceAge {};
    uint32_t ageCounter = 0;
    std::array<Voice, 2> patVoices;
    int patCur = 0;

    // pattern clock
    bool wasRunning = false, prevPatOn = false;
    double freeBeat = 0;

    // capture
    std::atomic<bool> captureArmed { false };
    std::atomic<int> captureState { CapIdle };
    std::vector<float> capL, capR;
    int capLen = 0, capPos = 0, capBars = 2;
    double capBpm = 120;
    bool prevCaptureParam = false;

    // global fx
    Svf svfL, svfR;
    float lastCutoff = -1, holdL = 0, holdR = 0, holdCount = 0;
    float outGainSmoothed = 1, mixSmoothed = 1;
    std::vector<float> wetL, wetR;
};
} // namespace cs
