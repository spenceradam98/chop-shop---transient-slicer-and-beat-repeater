#include "Engine.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace cs
{
int paramIndex (const char* id)
{
    const auto* d = paramDefs();
    for (int i = 0; i < NumParams; ++i)
        if (std::strcmp (d[i].id, id) == 0) return i;
    return -1;
}

Engine::Engine()
{
    const auto* d = paramDefs();
    for (int i = 0; i < NumParams; ++i) params[(size_t) i] = d[i].def;
    for (auto& r : retired) r.store (nullptr);
    for (auto& h : hits) h.store (0);
}

Engine::~Engine()
{
    delete kit;
    delete pending.exchange (nullptr);
    delete retiring;
    for (auto& r : retired) delete r.exchange (nullptr);
}

void Engine::prepare (double sampleRate, int maxBlock, bool fxBuild)
{
    sr = sampleRate;
    fx = fxBuild;
    for (auto& v : midiVoices) v.prepare (sr);
    for (auto& v : patVoices) v.prepare (sr);
    wetL.assign ((size_t) std::max (1, maxBlock), 0.0f);
    wetR.assign ((size_t) std::max (1, maxBlock), 0.0f);
    if (fx)
    {
        const size_t cap = (size_t) (sr * 33.0);   // 8 bars down to ~58 bpm
        if (captureState.load() != CapReady) { capL.assign (cap, 0.0f); capR.assign (cap, 0.0f); }
    }
    svfL.reset(); svfR.reset(); lastCutoff = -1;
    wasRunning = false;
}

// ---------------------------------------------------------------- kit hand-over
void Engine::submitKit (std::unique_ptr<Kit> k, bool atDownbeat)
{
    collectGarbage();
    pendingAtDownbeat.store (atDownbeat);
    delete pending.exchange (k.release());   // an unclaimed older pending kit is simply replaced
}

void Engine::collectGarbage()
{
    for (auto& r : retired) delete r.exchange (nullptr);
}

void Engine::swapKit (Kit* next)
{
    auto retire = [this] (Kit* k)
    {
        if (k == nullptr) return;
        for (auto& r : retired)
        {
            Kit* expected = nullptr;
            if (r.compare_exchange_strong (expected, k)) return;
        }
        // no free slot: never happens in practice (the UI collects 30 times a second); keep it alive
        // by parking it in the retiring slot rather than freeing on the audio thread
        if (retiring == nullptr) retiring = k;
    };

    Kit* old = kit;
    kit = next;
    if (old == nullptr) return;
    const bool sameSample = next != nullptr && old->sample.get() == next->sample.get();
    if (sameSample) { retire (old); return; }

    // voices still reading the old audio fade out; the old kit is retired once they have
    if (retiring != nullptr)
    {
        for (auto* pool : { midiVoices.data(), patVoices.data() })
            for (int i = 0; i < (pool == midiVoices.data() ? kMidiVoices : 2); ++i)
                if (pool[i].isActive() && pool[i].source() == retiring->sample.get()) pool[i].reset();
        Kit* r = retiring; retiring = nullptr; retire (r);
    }
    for (auto& v : midiVoices) if (v.isActive()) v.release();
    for (auto& v : patVoices) if (v.isActive()) v.release();
    retiring = old;
}

// ---------------------------------------------------------------- capture (FX build)
bool Engine::takeCapture (std::vector<float>& l, std::vector<float>& r, double& bpm, int& bars)
{
    if (captureState.load() != CapReady) return false;
    l.assign (capL.begin(), capL.begin() + capLen);
    r.assign (capR.begin(), capR.begin() + capLen);
    bpm = capBpm; bars = capBars;
    captureState.store (CapIdle);
    captureStatus.store (CapIdle);
    return true;
}

void Engine::handleCapture (const float* inL, const float* inR, int n, const Transport& tp)
{
    if (! fx || capL.empty()) return;
    const bool capParam = params[Capture] > 0.5f;
    if (capParam && ! prevCaptureParam) captureArmed.store (true);
    prevCaptureParam = capParam;

    int state = captureState.load();
    if (captureArmed.load() && state == CapIdle)
    {
        captureArmed.store (false);
        static const int barsFor[] = { 1, 2, 4, 8 };
        capBars = barsFor[std::clamp ((int) std::lround (params[CaptureBars]), 0, 3)];
        capBpm = tp.hasInfo && tp.bpm > 20 ? tp.bpm : 120.0;
        capLen = std::min ((int) capL.size(), (int) std::lround (capBars * 4.0 * 60.0 / capBpm * sr));
        capPos = 0;
        state = (tp.hasInfo && tp.playing) ? CapWaiting : CapRecording;
    }

    int from = 0;
    if (state == CapWaiting)
    {
        if (! tp.playing) return;
        const double perSample = tp.bpm / 60.0 / sr;
        const double barLen = std::max (1, tp.barBeats);
        double nextBar = std::ceil (tp.ppq / barLen - 1e-9) * barLen;
        const double end = tp.ppq + n * perSample;
        if (nextBar >= end) { captureState.store (state); captureStatus.store (state); return; }
        from = std::clamp ((int) ((nextBar - tp.ppq) / perSample), 0, n);
        state = CapRecording;
    }
    if (state == CapRecording)
    {
        const int count = std::min (n - from, capLen - capPos);
        for (int i = 0; i < count; ++i)
        {
            const float l = inL ? inL[from + i] : 0.0f;
            capL[(size_t) (capPos + i)] = l;
            capR[(size_t) (capPos + i)] = inR ? inR[from + i] : l;
        }
        capPos += count;
        captureProgress.store (capLen > 0 ? (float) capPos / (float) capLen : 1.0f);
        if (capPos >= capLen) state = CapReady;
    }
    captureState.store (state);
    captureStatus.store (state);
}

// ---------------------------------------------------------------- triggering
double Engine::stretchRatio (double hostBpm) const
{
    if (kit == nullptr || kit->bpm <= 0) return 1.0;
    return hostBpm / kit->bpm;
}

void Engine::noteOn (int note, float velocity, double hostBpm)
{
    if (kit == nullptr || kit->empty()) return;
    const int idx = note - (int) std::lround (params[RootNote]);
    if (idx < 0 || idx >= (int) kit->slices.size()) return;
    const auto& s = kit->settingsFor (idx);
    const auto& sl = kit->slices[(size_t) idx];
    const bool mono = params[Polyphony] > 0.5f;
    const bool choke = params[Choke] > 0.5f;
    const int mode = (int) std::lround (params[Stretch]);
    const double pitch = std::pow (2.0, s.pitch / 12.0);
    const double tr = stretchRatio (hostBpm);

    for (auto& v : midiVoices)
        if (v.isActive() && (mono || (choke && v.slice() == idx))) v.release();

    int pick = -1; uint32_t oldest = UINT32_MAX;
    for (int i = 0; i < kMidiVoices; ++i)
    {
        if (! midiVoices[(size_t) i].isActive()) { pick = i; break; }
        if (voiceAge[(size_t) i] < oldest) { oldest = voiceAge[(size_t) i]; pick = i; }
    }
    VoiceStart vs;
    vs.sliceIndex = idx; vs.start = sl.start; vs.end = sl.end;
    vs.gain = velocity * dbToGain (s.level);
    vs.reverse = s.reverse;
    vs.attackMs = s.attack; vs.decay = s.decay;
    vs.rate = mode == 1 ? pitch * tr : pitch;
    if (mode == 2) { vs.stretch = true; vs.rate = tr; vs.pitch = pitch; }
    // audio recorded at another sample rate plays at its own speed
    const double srcRatio = kit->sample->sampleRate / sr;
    vs.rate *= srcRatio; vs.pitch *= srcRatio;
    midiVoices[(size_t) pick].start (kit->sample.get(), vs);
    voiceAge[(size_t) pick] = ++ageCounter;
    hits[(size_t) idx].fetch_add (1);
}

void Engine::triggerStep (long long cycle, int step, int, double stepLenSamples, double hostSamplesPerBeat)
{
    PatternSettings ps;
    ps.order = (int) std::lround (params[Order]);
    ps.swing = params[Swing];
    ps.mutation = params[Mutation];
    ps.seed = (int) std::lround (params[Seed]);
    ps.lock = params[Lock] > 0.5f;
    const StepPlan p = planStep (*kit, ps, cycle, step);
    if (! p.play) return;

    const auto& sl = kit->slices[(size_t) p.slice];
    const auto& s = kit->settingsFor (p.slice);
    static const double speeds[] = { 0.5, 1.0, 2.0 };
    const double speed = speeds[std::clamp ((int) std::lround (params[Speed]), 0, 2)];
    const int mode = (int) std::lround (params[Stretch]);
    const double pitch = std::pow (2.0, p.pitch / 12.0);
    const double tr = kit->bpm > 0 ? (60.0 * sr / hostSamplesPerBeat) * speed / kit->bpm : 1.0;

    VoiceStart vs;
    vs.sliceIndex = p.slice; vs.start = sl.start; vs.end = sl.end;
    vs.gain = p.gain;
    vs.reverse = p.reverse;
    vs.attackMs = s.attack; vs.decay = s.decay;
    vs.rate = mode == 1 ? pitch * tr : pitch;
    if (mode == 2) { vs.stretch = true; vs.rate = tr; vs.pitch = pitch; }
    // audio recorded at another sample rate plays at its own speed
    const double srcRatio = kit->sample->sampleRate / sr;
    vs.rate *= srcRatio; vs.pitch *= srcRatio;
    vs.maxLength = stepLenSamples + 0.003 * sr;
    if (p.repeat) vs.repeatInterval = repeatBeats (p.repeatRate) * hostSamplesPerBeat;
    vs.drop = p.drop; vs.dropDepth = p.dropDepth;

    patVoices[(size_t) patCur].release();
    patCur ^= 1;
    patVoices[(size_t) patCur].start (kit->sample.get(), vs);
    hits[(size_t) p.slice].fetch_add (1);
    playingSlice.store (p.slice);
}

void Engine::renderSegment (float* outL, float* outR, int from, int to)
{
    if (to <= from) return;
    for (auto& v : midiVoices) if (v.isActive()) v.render (outL + from, outR + from, to - from);
    for (auto& v : patVoices) if (v.isActive()) v.render (outL + from, outR + from, to - from);
}

// ---------------------------------------------------------------- process
void Engine::process (const float* inL, const float* inR, float* outL, float* outR, int n,
                      const Transport& tp, const NoteEvent* events, int numEvents)
{
    if (n <= 0) return;
    if ((int) wetL.size() < n) n = (int) wetL.size();   // never allocate here; the wrapper splits big blocks
    handleCapture (inL, inR, n, tp);

    // finish retiring a kit once no voice reads its audio
    if (retiring != nullptr)
    {
        bool used = false;
        for (auto& v : midiVoices) used |= v.isActive() && v.source() == retiring->sample.get();
        for (auto& v : patVoices) used |= v.isActive() && v.source() == retiring->sample.get();
        if (! used)
            for (auto& r : retired)
            {
                Kit* expected = nullptr;
                if (r.compare_exchange_strong (expected, retiring)) { retiring = nullptr; break; }
            }
    }

    std::fill (wetL.begin(), wetL.begin() + n, 0.0f);
    std::fill (wetR.begin(), wetR.begin() + n, 0.0f);

    const double hostBpm = tp.hasInfo && tp.bpm > 20 ? tp.bpm : (kit != nullptr ? kit->bpm : 120.0);
    const double hostSamplesPerBeat = 60.0 * sr / hostBpm;
    static const double speeds[] = { 0.5, 1.0, 2.0 };
    const double speed = speeds[std::clamp ((int) std::lround (params[Speed]), 0, 2)];
    const bool patOn = params[PatOn] > 0.5f;

    // pattern clock: host position, or a free-running clock when not following the host
    bool running = false;
    double beat0 = 0;
    if (patOn)
    {
        if (params[FollowHost] > 0.5f && tp.hasInfo) { running = tp.playing; beat0 = tp.ppq; }
        else
        {
            if (! prevPatOn || ! wasRunning) freeBeat = 0;
            running = true; beat0 = freeBeat;
            freeBeat += n / hostSamplesPerBeat;
        }
    }
    prevPatOn = patOn;

    // a kit waiting for "now" goes in straight away; one waiting for the downbeat goes in now
    // only if the pattern is not running
    if (pending.load() != nullptr && (! pendingAtDownbeat.load() || ! running || kit == nullptr || kit->empty()))
        swapKit (pending.exchange (nullptr));

    if (! running && wasRunning) { for (auto& v : patVoices) v.release(); playingSlice.store (-1); }
    wasRunning = running;

    // ---- gather events: MIDI notes, pattern steps, a downbeat kit swap
    struct Ev { int offset; int kind; int a; long long cycle; float vel; double len; };
    constexpr int kMaxEv = 256;
    Ev ev[kMaxEv]; int ne = 0;
    for (int i = 0; i < numEvents && ne < kMaxEv; ++i)
        if (events[i].velocity > 0) ev[ne++] = { std::clamp (events[i].offset, 0, n - 1), 0, events[i].note, 0, events[i].velocity, 0 };

    const double inc = speed / hostSamplesPerBeat;   // pattern beats per sample
    const double pb0 = beat0 * speed, pb1 = pb0 + n * inc;
    int swapAt = -1;
    if (running && kit != nullptr && ! kit->empty())
    {
        auto addSteps = [&] (const Kit& k, double a, double b)
        {
            const int count = stepCount (k);
            const double L = k.loopBeats();
            const float sw = params[Swing];
            for (long long c = (long long) std::floor (a / L); (double) c * L < b && ne < kMaxEv; ++c)
                for (int s = 0; s < count && ne < kMaxEv; ++s)
                {
                    const double t = (double) c * L + stepBeat (k, s, sw);
                    if (t < a || t >= b) continue;
                    const double next = s + 1 < count ? (double) c * L + stepBeat (k, s + 1, sw) : (double) (c + 1) * L + stepBeat (k, 0, sw);
                    ev[ne++] = { std::clamp ((int) ((t - pb0) / inc), 0, n - 1), 1, s, c, 0, std::max (1.0, (next - t) / inc) };
                }
        };
        double split = pb1;
        if (pending.load() != nullptr && pendingAtDownbeat.load())
        {
            const double L = kit->loopBeats();
            const double boundary = std::ceil (pb0 / L - 1e-9) * L;
            if (boundary < pb1) { split = boundary; swapAt = std::clamp ((int) ((boundary - pb0) / inc), 0, n - 1); }
        }
        addSteps (*kit, pb0, split);
        if (swapAt >= 0 && ne < kMaxEv) ev[ne++] = { swapAt, 2, 0, (long long) std::floor (split), 0, split };
    }
    std::stable_sort (ev, ev + ne, [] (const Ev& x, const Ev& y) { return x.offset != y.offset ? x.offset < y.offset : x.kind > y.kind; });

    int pos = 0;
    for (int i = 0; i < ne; ++i)
    {
        renderSegment (wetL.data(), wetR.data(), pos, ev[i].offset);
        pos = ev[i].offset;
        if (ev[i].kind == 0) noteOn (ev[i].a, ev[i].vel, hostBpm);
        else if (ev[i].kind == 1) triggerStep (ev[i].cycle, ev[i].a, pos, ev[i].len, hostSamplesPerBeat);
        else if (ev[i].kind == 2)
        {
            swapKit (pending.exchange (nullptr));
            // steps of the new kit in the rest of the block
            if (kit != nullptr && ! kit->empty())
            {
                const int count = stepCount (*kit);
                const double L = kit->loopBeats();
                const float sw = params[Swing];
                const double a = ev[i].len;
                for (long long c = (long long) std::floor (a / L); (double) c * L < pb1 && ne < kMaxEv; ++c)
                    for (int s = 0; s < count && ne < kMaxEv; ++s)
                    {
                        const double t = (double) c * L + stepBeat (*kit, s, sw);
                        if (t < a || t >= pb1) continue;
                        const double next = s + 1 < count ? (double) c * L + stepBeat (*kit, s + 1, sw) : (double) (c + 1) * L + stepBeat (*kit, 0, sw);
                        ev[ne++] = { std::clamp ((int) ((t - pb0) / inc), 0, n - 1), 1, s, c, 0, std::max (1.0, (next - t) / inc) };
                    }
                std::stable_sort (ev + i + 1, ev + ne, [] (const Ev& x, const Ev& y) { return x.offset < y.offset; });
            }
        }
    }
    renderSegment (wetL.data(), wetR.data(), pos, n);

    // UI feedback
    const auto& pv = patVoices[(size_t) patCur];
    if (pv.isActive()) playCursor.store ((float) pv.sourcePosition());
    else
    {
        float c = -1.0f; uint32_t newest = 0;
        for (int i = 0; i < kMidiVoices; ++i)
            if (midiVoices[(size_t) i].isActive() && voiceAge[(size_t) i] >= newest) { newest = voiceAge[(size_t) i]; c = (float) midiVoices[(size_t) i].sourcePosition(); }
        playCursor.store (c);
        if (c < 0) playingSlice.store (-1);
    }

    applyGlobal (outL, outR, inL, inR, n);
}

void Engine::applyGlobal (float* outL, float* outR, const float* inL, const float* inR, int n)
{
    // filter: one knob, left = low-pass, right = high-pass
    const float f = params[Filter];
    float cutoff = 0;
    if (f < -0.02f) cutoff = 20000.0f * std::pow (150.0f / 20000.0f, -f);
    else if (f > 0.02f) cutoff = 20.0f * std::pow (4000.0f / 20.0f, f);
    if (cutoff > 0 && std::fabs (cutoff - lastCutoff) > 0.5f)
    {
        if (lastCutoff < 0) { svfL.reset(); svfR.reset(); }
        svfL.set (sr, cutoff, 0.9); svfR.set (sr, cutoff, 0.9);
        lastCutoff = cutoff;
    }
    if (cutoff <= 0) lastCutoff = -1;

    const float crush = params[Crush];
    const float levels = std::pow (2.0f, 15.0f - 12.0f * crush);
    const float holdLen = 1.0f + crush * crush * 20.0f;
    const float gTarget = dbToGain (params[Output]);
    const float mixTarget = fx ? params[Mix] : 1.0f;
    const bool passDry = fx && (kit == nullptr || kit->empty());
    float peak = 0;

    for (int i = 0; i < n; ++i)
    {
        float l = wetL[(size_t) i], r = wetR[(size_t) i];
        if (cutoff > 0)
        {
            float hl, hr;
            const float ll = svfL.process (l, hl), lr = svfR.process (r, hr);
            l = f < 0 ? ll : hl; r = f < 0 ? lr : hr;
        }
        if (crush > 0.001f)
        {
            if ((holdCount += 1.0f) >= holdLen) { holdCount -= holdLen; holdL = std::round (l * levels) / levels; holdR = std::round (r * levels) / levels; }
            l = holdL; r = holdR;
        }
        outGainSmoothed += 0.002f * (gTarget - outGainSmoothed);
        mixSmoothed += 0.002f * (mixTarget - mixSmoothed);
        l *= outGainSmoothed; r *= outGainSmoothed;
        if (fx)
        {
            const float dl = inL ? inL[i] : 0.0f, dr = inR ? inR[i] : dl;
            if (passDry) { l = dl; r = dr; }
            else { l = dl * (1.0f - mixSmoothed) + l * mixSmoothed; r = dr * (1.0f - mixSmoothed) + r * mixSmoothed; }
        }
        outL[i] = l; outR[i] = r;
        peak = std::max (peak, std::max (std::fabs (l), std::fabs (r)));
    }
    outLevel.store (std::max (peak, outLevel.load() * 0.9f));
}
} // namespace cs
