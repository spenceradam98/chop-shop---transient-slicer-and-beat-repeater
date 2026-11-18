#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Analysis.h"
#include "Breaks.h"
#include "Presets.h"

using namespace juce;

namespace cs
{
namespace
{
String formatValue (const ParamDef& d, float v)
{
    const String unit (d.unit);
    if (unit == "dB") return (v > 0 ? "+" : "") + String (v, 1) + " dB";
    if (unit == "ms") return String (roundToInt (v)) + " ms";
    if (unit == "bip")
    {
        if (std::abs (v) < 0.02f) return "Open";
        return (v < 0 ? "LP " : "HP ") + String (roundToInt (std::abs (v) * 100.0f)) + "%";
    }
    return String (roundToInt (v * 100.0f)) + "%";
}

constexpr int kMaxEmbedSeconds = 60;
const Identifier idKit ("Kit"), idAudio ("Audio"), idSlice ("Slice");
} // namespace

AudioProcessorValueTreeState::ParameterLayout ChopShopProcessor::createLayout()
{
    std::vector<std::unique_ptr<RangedAudioParameter>> p;
    const auto* defs = paramDefs();
    for (int i = 0; i < NumParams; ++i)
    {
        const auto& d = defs[i];
        const ParameterID pid { d.id, 1 };
        switch (d.kind)
        {
            case Kind::Bool:
                p.push_back (std::make_unique<AudioParameterBool> (pid, d.name, d.def > 0.5f));
                break;
            case Kind::Choice:
                p.push_back (std::make_unique<AudioParameterChoice> (pid, d.name, StringArray::fromTokens (d.choices, "|", ""), (int) d.def));
                break;
            case Kind::Int:
            {
                auto attrs = AudioParameterIntAttributes();
                if (String (d.unit) == "note")
                    attrs = attrs.withStringFromValueFunction ([] (int v, int) { return MidiMessage::getMidiNoteName (v, true, true, 3); });
                p.push_back (std::make_unique<AudioParameterInt> (pid, d.name, (int) d.min, (int) d.max, (int) d.def, attrs));
                break;
            }
            case Kind::Float:
            default:
            {
                NormalisableRange<float> range (d.min, d.max);
                if (d.centre > d.min && d.centre < d.max) range.setSkewForCentre (d.centre);
                const auto attrs = AudioParameterFloatAttributes()
                                       .withLabel (String (d.unit) == "bip" ? String() : String (d.unit))
                                       .withStringFromValueFunction ([d] (float v, int) { return formatValue (d, v); });
                p.push_back (std::make_unique<AudioParameterFloat> (pid, d.name, range, d.def, attrs));
                break;
            }
        }
    }
    return { p.begin(), p.end() };
}

AudioProcessor::BusesProperties ChopShopProcessor::makeBuses()
{
    if (isFx) return BusesProperties()
                   .withInput ("Input", AudioChannelSet::stereo(), true)
                   .withOutput ("Output", AudioChannelSet::stereo(), true);
    return BusesProperties().withOutput ("Output", AudioChannelSet::stereo(), true);
}

ChopShopProcessor::ChopShopProcessor()
    : AudioProcessor (makeBuses()),
      apvts (*this, nullptr, "ChopShop", createLayout())
{
    formats.registerBasicFormats();
    const auto* defs = paramDefs();
    for (int i = 0; i < NumParams; ++i) raw[(size_t) i] = apvts.getRawParameterValue (defs[i].id);
    editKit.settings.resize ((size_t) kMaxSlices);
    if (! isFx) loadFactoryBreak (0);
    lastSliceParams = { raw[SliceMode]->load(), raw[Sensitivity]->load(), raw[MinSlice]->load() };
    startTimerHz (30);
}

ChopShopProcessor::~ChopShopProcessor() { stopTimer(); }

bool ChopShopProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    if (out != AudioChannelSet::mono() && out != AudioChannelSet::stereo()) return false;
    if (isFx)
    {
        const auto in = layouts.getMainInputChannelSet();
        return in == AudioChannelSet::mono() || in == AudioChannelSet::stereo();
    }
    return true;
}

void ChopShopProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxBlock = jmax (32, samplesPerBlock);
    for (int i = 0; i < NumParams; ++i) engine.setParam (i, raw[(size_t) i]->load());
    engine.prepare (sampleRate, maxBlock, isFx);
    scratchL.assign ((size_t) maxBlock, 0.0f);
    scratchR.assign ((size_t) maxBlock, 0.0f);
}

void ChopShopProcessor::processBlock (AudioBuffer<float>& buffer, MidiBuffer& midi)
{
    ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    const int inCh = getTotalNumInputChannels(), outCh = getTotalNumOutputChannels();
    if (n == 0 || outCh == 0) return;

    for (int i = 0; i < NumParams; ++i) engine.setParam (i, raw[(size_t) i]->load());

    Transport tp;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            tp.hasInfo = pos->getBpm().hasValue() || pos->getPpqPosition().hasValue();
            tp.playing = pos->getIsPlaying();
            tp.bpm = pos->getBpm().orFallback (120.0);
            tp.ppq = pos->getPpqPosition().orFallback (0.0);
            if (auto ts = pos->getTimeSignature()) tp.barBeats = jmax (1, roundToInt (ts->numerator * 4.0 / jmax (1, ts->denominator)));
        }

    constexpr int kMaxNotes = 256;
    NoteEvent notes[kMaxNotes];
    int numNotes = 0;
    for (const auto meta : midi)
    {
        const auto m = meta.getMessage();
        if (m.isNoteOn() && numNotes < kMaxNotes) notes[numNotes++] = { meta.samplePosition, m.getNoteNumber(), m.getFloatVelocity() };
    }
    const int aud = auditionSlice.exchange (-1);
    if (aud >= 0 && numNotes < kMaxNotes) notes[numNotes++] = { 0, roundToInt (raw[RootNote]->load()) + aud, 0.9f };

    const float* inL = (isFx && inCh > 0) ? buffer.getReadPointer (0) : nullptr;
    const float* inR = (isFx && inCh > 1) ? buffer.getReadPointer (1) : inL;
    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;

    for (int start = 0; start < n; start += maxBlock)
    {
        const int len = jmin (maxBlock, n - start);
        NoteEvent chunk[kMaxNotes]; int nc = 0;
        for (int i = 0; i < numNotes; ++i)
            if (notes[i].offset >= start && notes[i].offset < start + len) chunk[nc++] = { notes[i].offset - start, notes[i].note, notes[i].velocity };
        Transport t = tp;
        t.ppq = tp.ppq + start / sr * tp.bpm / 60.0;
        engine.process (inL ? inL + start : nullptr, inR ? inR + start : nullptr, scratchL.data(), scratchR.data(), len, t, chunk, nc);
        if (outCh == 1)
        {
            auto* o = buffer.getWritePointer (0, start);
            for (int i = 0; i < len; ++i) o[i] = 0.5f * (scratchL[(size_t) i] + scratchR[(size_t) i]);
        }
        else
        {
            buffer.copyFrom (0, start, scratchL.data(), len);
            buffer.copyFrom (1, start, scratchR.data(), len);
            for (int c = 2; c < outCh; ++c) buffer.clear (c, start, len);
        }
    }
    midi.clear();
}

// ---------------------------------------------------------------- the kit
void ChopShopProcessor::pushKit (bool atDownbeat)
{
    engine.submitKit (std::make_unique<Kit> (editKit), atDownbeat);
    ++version;
}

std::vector<int> ChopShopProcessor::currentMarkers() const
{
    std::vector<int> m;
    for (const auto& s : editKit.slices) m.push_back (s.start);
    return m;
}

void ChopShopProcessor::setSample (std::shared_ptr<Sample> s, double bpm, int bars, bool atDownbeat)
{
    if (s == nullptr || s->length() < 64) return;
    int mode = roundToInt (raw[SliceMode]->load());
    if (mode == ModeManual)
    {
        mode = ModeTransient;   // old markers mean nothing for new audio
        if (auto* prm = apvts.getParameter ("slice_mode")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) ModeTransient));
    }
    auto settings = editKit.settings;   // per-slice settings (the pattern) carry over to the new break
    sampleName = String (s->name);
    editKit = makeKit (s, mode, raw[Sensitivity]->load(), raw[MinSlice]->load(), bpm, bars);
    if (settings.size() == editKit.settings.size()) editKit.settings = settings;
    lastSliceParams = { raw[SliceMode]->load(), raw[Sensitivity]->load(), raw[MinSlice]->load() };
    selectedSlice = jlimit (0, jmax (0, (int) editKit.slices.size() - 1), selectedSlice);
    pushKit (atDownbeat);
}

bool ChopShopProcessor::loadAudioFile (const File& f)
{
    std::unique_ptr<AudioFormatReader> reader (formats.createReaderFor (f));
    if (reader == nullptr || reader->lengthInSamples < 64) return false;
    const int len = (int) jmin<int64> (reader->lengthInSamples, (int64) (reader->sampleRate * kMaxEmbedSeconds));
    AudioBuffer<float> buf ((int) jmax (1u, jmin (2u, reader->numChannels)), len);
    reader->read (&buf, 0, len, 0, true, buf.getNumChannels() > 1);
    auto s = std::make_shared<Sample>();
    s->sampleRate = reader->sampleRate;
    s->name = f.getFileNameWithoutExtension().toStdString();
    s->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
    s->r.assign (buf.getReadPointer (buf.getNumChannels() - 1), buf.getReadPointer (buf.getNumChannels() - 1) + len);
    s->mono.resize ((size_t) len);
    for (int i = 0; i < len; ++i) s->mono[(size_t) i] = 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]);
    setSample (s, 0, 0, false);
    return true;
}

void ChopShopProcessor::loadFactoryBreak (int index)
{
    if (! isPositiveAndBelow (index, (int) factoryBreaks().size())) return;
    const auto& info = factoryBreaks()[(size_t) index];
    setSample (renderBreak (index, 48000.0), info.bpm, info.bars, false);
}

void ChopShopProcessor::reslice()
{
    if (editKit.sample == nullptr) return;
    sliceKit (editKit, roundToInt (raw[SliceMode]->load()), raw[Sensitivity]->load(), raw[MinSlice]->load(), currentMarkers());
    selectedSlice = jlimit (0, jmax (0, (int) editKit.slices.size() - 1), selectedSlice);
    pushKit (false);
}

void ChopShopProcessor::setTempo (double bpm)
{
    if (editKit.sample == nullptr) return;
    editKit.bpm = jlimit (40.0, 250.0, bpm);
    editKit.bars = jmax (1, (int) std::lround (editKit.sample->length() / editKit.sample->sampleRate * editKit.bpm / 240.0));
    editKit.tempoFromUser = true;
    if (roundToInt (raw[SliceMode]->load()) == ModeGrid8 || roundToInt (raw[SliceMode]->load()) == ModeGrid16) reslice();
    else pushKit (false);
}

void ChopShopProcessor::setBars (int bars)
{
    if (editKit.sample == nullptr) return;
    const double dur = editKit.sample->length() / editKit.sample->sampleRate;
    setTempo (240.0 * jlimit (1, 64, bars) / dur);
}

void ChopShopProcessor::halveTempo (bool halve) { setTempo (editKit.bpm * (halve ? 0.5 : 2.0)); }

static void setSliceModeManual (AudioProcessorValueTreeState& apvts)
{
    if (auto* prm = apvts.getParameter ("slice_mode")) prm->setValueNotifyingHost (prm->convertTo0to1 ((float) ModeManual));
}

void ChopShopProcessor::addMarker (int pos)
{
    if (editKit.sample == nullptr || (int) editKit.slices.size() >= kMaxSlices) return;
    pos = nudgeToZeroCrossing (editKit.sample->mono, jlimit (1, editKit.sample->length() - 65, pos), 32);
    int k = 0;
    while (k < (int) editKit.slices.size() && editKit.slices[(size_t) k].start < pos) ++k;
    auto m = currentMarkers();
    m.push_back (pos);
    editKit.settings.insert (editKit.settings.begin() + k, SliceSettings());
    editKit.settings.resize ((size_t) kMaxSlices);
    editKit.slices = slicesFromMarkers (m, editKit.sample->length());
    setSliceModeManual (apvts);
    lastSliceParams[0] = (float) ModeManual;
    selectedSlice = jlimit (0, (int) editKit.slices.size() - 1, k);
    pushKit (false);
}

void ChopShopProcessor::moveMarker (int sliceIndex, int pos)
{
    if (editKit.sample == nullptr || sliceIndex <= 0 || sliceIndex >= (int) editKit.slices.size()) return;
    const int lo = editKit.slices[(size_t) sliceIndex - 1].start + 64;
    const int hi = editKit.slices[(size_t) sliceIndex].end - 64;
    if (hi <= lo) return;
    editKit.slices[(size_t) sliceIndex].start = jlimit (lo, hi, pos);
    editKit.slices[(size_t) sliceIndex - 1].end = editKit.slices[(size_t) sliceIndex].start;
    setSliceModeManual (apvts);
    lastSliceParams[0] = (float) ModeManual;
    pushKit (false);
}

void ChopShopProcessor::removeMarker (int sliceIndex)
{
    if (editKit.sample == nullptr || sliceIndex <= 0 || sliceIndex >= (int) editKit.slices.size()) return;
    auto m = currentMarkers();
    m.erase (m.begin() + sliceIndex);
    editKit.settings.erase (editKit.settings.begin() + sliceIndex);
    editKit.settings.resize ((size_t) kMaxSlices);
    editKit.slices = slicesFromMarkers (m, editKit.sample->length());
    setSliceModeManual (apvts);
    lastSliceParams[0] = (float) ModeManual;
    selectedSlice = jlimit (0, (int) editKit.slices.size() - 1, selectedSlice);
    pushKit (false);
}

void ChopShopProcessor::setSliceSettings (int slice, const SliceSettings& s)
{
    if (! isPositiveAndBelow (slice, kMaxSlices)) return;
    if ((int) editKit.settings.size() < kMaxSlices) editKit.settings.resize ((size_t) kMaxSlices);
    if (editKit.settings[(size_t) slice] == s) return;
    editKit.settings[(size_t) slice] = s;
    pushKit (false);
}

void ChopShopProcessor::timerCallback()
{
    engine.collectGarbage();

    const std::array<float, 3> now { raw[SliceMode]->load(), raw[Sensitivity]->load(), raw[MinSlice]->load() };
    if (now != lastSliceParams)
    {
        const bool onlyTuning = roundToInt (now[0]) == roundToInt (lastSliceParams[0]);
        lastSliceParams = now;
        // sensitivity / min length only matter for transient mode
        if (! onlyTuning || roundToInt (now[0]) == ModeTransient) reslice();
    }

    if (isFx && engine.captureReady())
    {
        std::vector<float> l, r; double bpm = 120; int bars = 1;
        if (engine.takeCapture (l, r, bpm, bars) && l.size() > 64)
        {
            auto s = std::make_shared<Sample>();
            s->sampleRate = getSampleRate() > 0 ? getSampleRate() : 44100.0;
            s->name = "Capture " + Time::getCurrentTime().formatted ("%H:%M:%S").toStdString();
            s->l = std::move (l); s->r = std::move (r);
            s->mono.resize (s->l.size());
            for (size_t i = 0; i < s->l.size(); ++i) s->mono[i] = 0.5f * (s->l[i] + s->r[i]);
            setSample (s, bpm, bars, true);   // swaps in on the next loop downbeat
        }
    }
}

// ---------------------------------------------------------------- export
juce::File ChopShopProcessor::exportSliceWav (int slice)
{
    if (editKit.sample == nullptr || ! isPositiveAndBelow (slice, (int) editKit.slices.size())) return {};
    const auto& sl = editKit.slices[(size_t) slice];
    const auto& st = editKit.settingsFor (slice);
    const int len = sl.length();
    AudioBuffer<float> buf (2, len);
    const float gain = dbToGain (st.level);
    const int fade = jmin (len / 4, (int) (0.002 * editKit.sample->sampleRate));
    for (int i = 0; i < len; ++i)
    {
        const int src = st.reverse ? sl.end - 1 - i : sl.start + i;
        float env = gain;
        if (i >= len - fade) env *= (float) (len - i) / (float) jmax (1, fade);
        if (st.reverse && i < fade) env *= (float) i / (float) jmax (1, fade);
        buf.setSample (0, i, editKit.sample->l[(size_t) src] * env);
        buf.setSample (1, i, editKit.sample->r[(size_t) src] * env);
    }
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Chop Shop");
    dir.createDirectory();
    auto base = File::createLegalFileName (sampleName.isEmpty() ? "Break" : sampleName);
    auto f = dir.getChildFile (base + " slice " + String (slice + 1).paddedLeft ('0', 2) + ".wav");
    f.deleteFile();
    WavAudioFormat wav;
    std::unique_ptr<OutputStream> os (f.createOutputStream());
    if (os == nullptr) return {};
    std::unique_ptr<AudioFormatWriter> w (wav.createWriterFor (os.get(), editKit.sample->sampleRate, 2, 24, {}, 0));
    if (w == nullptr) return {};
    os.release();
    w->writeFromAudioSampleBuffer (buf, 0, len);
    return f;
}

juce::File ChopShopProcessor::exportPatternMidi (int cycles)
{
    if (editKit.empty()) return {};
    constexpr int ppq = 960;
    MidiMessageSequence seq;
    seq.addEvent (MidiMessage::tempoMetaEvent ((int) std::lround (60000000.0 / editKit.bpm)), 0);
    seq.addEvent (MidiMessage::timeSignatureMetaEvent (4, 4), 0);
    PatternSettings ps;
    ps.order = roundToInt (raw[Order]->load());
    ps.swing = raw[Swing]->load();
    ps.mutation = raw[Mutation]->load();
    ps.seed = roundToInt (raw[Seed]->load());
    ps.lock = raw[Lock]->load() > 0.5f;
    const int root = roundToInt (raw[RootNote]->load());
    const int steps = stepCount (editKit);
    const double L = editKit.loopBeats();
    for (int c = 0; c < cycles; ++c)
        for (int s = 0; s < steps; ++s)
        {
            const auto p = planStep (editKit, ps, c, s);
            if (! p.play) continue;
            const double t = c * L + stepBeat (editKit, s, ps.swing);
            const double next = s + 1 < steps ? c * L + stepBeat (editKit, s + 1, ps.swing) : (c + 1) * L + stepBeat (editKit, 0, ps.swing);
            const int vel = jlimit (1, 127, roundToInt (100.0f * p.gain));
            const double each = p.repeat ? repeatBeats (p.repeatRate) : next - t;
            for (double b = t; b < next - 1e-6; b += each)
            {
                const double end = jmin (next, b + each) - 0.01;
                seq.addEvent (MidiMessage::noteOn (1, root + p.slice, (uint8) vel), b * ppq);
                seq.addEvent (MidiMessage::noteOff (1, root + p.slice), jmax (b + 0.01, end) * ppq);
                if (! p.repeat) break;
            }
        }
    seq.updateMatchedPairs();
    MidiFile mf;
    mf.setTicksPerQuarterNote (ppq);
    mf.addTrack (seq);
    auto dir = File::getSpecialLocation (File::tempDirectory).getChildFile ("Chop Shop");
    dir.createDirectory();
    auto f = dir.getChildFile (File::createLegalFileName ((sampleName.isEmpty() ? String ("Break") : sampleName) + " pattern seed " + String (ps.seed)) + ".mid");
    f.deleteFile();
    FileOutputStream os (f);
    if (! os.openedOk() || ! mf.writeTo (os)) return {};
    return f;
}

// ---------------------------------------------------------------- presets
int ChopShopProcessor::getNumPrograms() { return (int) patternPresets().size(); }

const String ChopShopProcessor::getProgramName (int index)
{
    const auto& ps = patternPresets();
    return isPositiveAndBelow (index, (int) ps.size()) ? String (ps[(size_t) index].name) : String();
}

std::array<float, NumParams> ChopShopProcessor::currentValues() const
{
    std::array<float, NumParams> v {};
    for (int i = 0; i < NumParams; ++i) v[(size_t) i] = raw[(size_t) i]->load();
    return v;
}

void ChopShopProcessor::applyValues (const std::array<float, NumParams>& v)
{
    const auto* defs = paramDefs();
    for (int i = 0; i < NumParams; ++i)
        if (auto* prm = apvts.getParameter (defs[i].id))
            prm->setValueNotifyingHost (prm->convertTo0to1 (v[(size_t) i]));
}

void ChopShopProcessor::loadFactoryPreset (int index)
{
    const auto& ps = patternPresets();
    if (! isPositiveAndBelow (index, (int) ps.size())) return;
    currentProgram = index;
    presetName = ps[(size_t) index].name;
    applyValues (presetValues (ps[(size_t) index], currentValues()));
    applyPresetRule (ps[(size_t) index], editKit);
    pushKit (false);
}

File ChopShopProcessor::userPresetFolder()
{
    return File::getSpecialLocation (File::userApplicationDataDirectory).getChildFile ("Chop Shop").getChildFile ("Presets");
}

Array<File> ChopShopProcessor::userPresets() const
{
    auto files = userPresetFolder().findChildFiles (File::findFiles, false, "*.cspreset");
    files.sort();
    return files;
}

static void writeSettings (XmlElement& e, const SliceSettings& s)
{
    e.setAttribute ("level", s.level); e.setAttribute ("pitch", s.pitch); e.setAttribute ("reverse", s.reverse);
    e.setAttribute ("attack", s.attack); e.setAttribute ("decay", s.decay); e.setAttribute ("prob", s.prob);
    e.setAttribute ("repeatProb", s.repeatProb); e.setAttribute ("repeatRate", s.repeatRate);
    e.setAttribute ("reverseProb", s.reverseProb); e.setAttribute ("dropProb", s.dropProb); e.setAttribute ("dropDepth", s.dropDepth);
}

static SliceSettings readSettings (const XmlElement& e)
{
    SliceSettings s;
    s.level = (float) e.getDoubleAttribute ("level", s.level);
    s.pitch = (float) e.getDoubleAttribute ("pitch", s.pitch);
    s.reverse = e.getBoolAttribute ("reverse", s.reverse);
    s.attack = (float) e.getDoubleAttribute ("attack", s.attack);
    s.decay = (float) e.getDoubleAttribute ("decay", s.decay);
    s.prob = (float) e.getDoubleAttribute ("prob", s.prob);
    s.repeatProb = (float) e.getDoubleAttribute ("repeatProb", s.repeatProb);
    s.repeatRate = jlimit (0, NumRepeatRates - 1, e.getIntAttribute ("repeatRate", s.repeatRate));
    s.reverseProb = (float) e.getDoubleAttribute ("reverseProb", s.reverseProb);
    s.dropProb = (float) e.getDoubleAttribute ("dropProb", s.dropProb);
    s.dropDepth = (float) e.getDoubleAttribute ("dropDepth", s.dropDepth);
    return s;
}

bool ChopShopProcessor::saveUserPreset (const String& name)
{
    const auto clean = File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    XmlElement xml ("ChopShopPreset");
    xml.setAttribute ("name", name.trim());
    xml.setAttribute ("version", 1);
    const auto* defs = paramDefs();
    for (int i = 0; i < NumParams; ++i)
    {
        if (i == SliceMode || i == Sensitivity || i == MinSlice || i == Capture) continue;   // presets are patterns, not slicing
        auto* e = xml.createNewChildElement ("param");
        e->setAttribute ("id", defs[i].id);
        e->setAttribute ("value", (double) raw[(size_t) i]->load());
    }
    for (int i = 0; i < kMaxSlices; ++i)
    {
        auto* e = xml.createNewChildElement ("slice");
        e->setAttribute ("index", i);
        writeSettings (*e, editKit.settingsFor (i));
    }
    userPresetFolder().createDirectory();
    if (! xml.writeTo (userPresetFolder().getChildFile (clean + ".cspreset"))) return false;
    presetName = name.trim();
    currentProgram = -1;
    return true;
}

bool ChopShopProcessor::loadUserPreset (const File& f)
{
    auto xml = XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("ChopShopPreset")) return false;
    auto v = currentValues();
    for (auto* e : xml->getChildWithTagNameIterator ("param"))
    {
        const int idx = paramIndex (e->getStringAttribute ("id").toRawUTF8());
        if (idx >= 0) v[(size_t) idx] = (float) e->getDoubleAttribute ("value", v[(size_t) idx]);
    }
    applyValues (v);
    if ((int) editKit.settings.size() < kMaxSlices) editKit.settings.resize ((size_t) kMaxSlices);
    for (auto* e : xml->getChildWithTagNameIterator ("slice"))
    {
        const int i = e->getIntAttribute ("index", -1);
        if (isPositiveAndBelow (i, kMaxSlices)) editKit.settings[(size_t) i] = readSettings (*e);
    }
    pushKit (false);
    presetName = xml->getStringAttribute ("name", f.getFileNameWithoutExtension());
    currentProgram = -1;
    return true;
}

void ChopShopProcessor::stepPreset (int delta)
{
    const int nf = getNumPrograms();
    const auto users = userPresets();
    const int total = nf + users.size();
    int cur = currentProgram;
    if (cur < 0)
    {
        cur = nf;
        for (int i = 0; i < users.size(); ++i)
            if (users[i].getFileNameWithoutExtension() == File::createLegalFileName (presetName)) cur = nf + i;
    }
    const int next = ((cur + delta) % total + total) % total;
    if (next < nf) loadFactoryPreset (next);
    else loadUserPreset (users[next - nf]);
}

// ---------------------------------------------------------------- state
void ChopShopProcessor::getStateInformation (MemoryBlock& destData)
{
    auto state = apvts.copyState();
    state.setProperty ("program", currentProgram, nullptr);
    state.setProperty ("presetName", presetName, nullptr);
    state.setProperty ("theme", themeIndex, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    state.setProperty ("selectedSlice", selectedSlice, nullptr);
    state.removeChild (state.getChildWithName (idKit), nullptr);
    state.removeChild (state.getChildWithName (idAudio), nullptr);

    ValueTree k (idKit);
    k.setProperty ("bpm", editKit.bpm, nullptr);
    k.setProperty ("bars", editKit.bars, nullptr);
    k.setProperty ("tempoFromUser", editKit.tempoFromUser, nullptr);
    k.setProperty ("name", sampleName, nullptr);
    StringArray markers;
    for (int m : currentMarkers()) markers.add (String (m));
    k.setProperty ("markers", markers.joinIntoString (","), nullptr);
    for (int i = 0; i < kMaxSlices; ++i)
    {
        XmlElement e ("s");
        writeSettings (e, editKit.settingsFor (i));
        ValueTree s (idSlice);
        for (int a = 0; a < e.getNumAttributes(); ++a) s.setProperty (e.getAttributeName (a), e.getAttributeValue (a), nullptr);
        k.appendChild (s, nullptr);
    }
    state.appendChild (k, nullptr);

    // the audio itself, so a session recalls its break (FLAC, 24-bit)
    if (editKit.sample != nullptr)
    {
        const auto& s = *editKit.sample;
        MemoryBlock flac;
        {
            FlacAudioFormat fmt;
            auto* mos = new MemoryOutputStream (flac, false);
            std::unique_ptr<AudioFormatWriter> w (fmt.createWriterFor (mos, s.sampleRate, 2, 24, {}, 0));
            if (w != nullptr)
            {
                const float* ch[] = { s.l.data(), s.r.data() };
                w->writeFromFloatArrays (ch, 2, s.length());
            }
            else delete mos;
        }
        ValueTree a (idAudio);
        a.setProperty ("sampleRate", s.sampleRate, nullptr);
        a.setProperty ("length", s.length(), nullptr);
        a.setProperty ("flac", flac.toBase64Encoding(), nullptr);
        state.appendChild (a, nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void ChopShopProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType())) return;
    auto tree = ValueTree::fromXml (*xml);
    currentProgram = tree.getProperty ("program", 0);
    presetName = tree.getProperty ("presetName", presetName).toString();
    themeIndex = tree.getProperty ("theme", 0);
    uiScale = (float) (double) tree.getProperty ("uiScale", 1.0);
    selectedSlice = tree.getProperty ("selectedSlice", 0);
    const auto k = tree.getChildWithName (idKit);
    const auto a = tree.getChildWithName (idAudio);
    tree.removeChild (k, nullptr);
    tree.removeChild (a, nullptr);
    apvts.replaceState (tree);

    std::shared_ptr<Sample> s;
    if (a.isValid())
    {
        MemoryBlock flac;
        if (flac.fromBase64Encoding (a.getProperty ("flac").toString()))
        {
            FlacAudioFormat fmt;
            std::unique_ptr<AudioFormatReader> r (fmt.createReaderFor (new MemoryInputStream (flac, false), true));
            if (r != nullptr && r->lengthInSamples > 0)
            {
                const int len = (int) r->lengthInSamples;
                AudioBuffer<float> buf (2, len);
                r->read (&buf, 0, len, 0, true, true);
                s = std::make_shared<Sample>();
                s->sampleRate = r->sampleRate;
                s->name = k.getProperty ("name").toString().toStdString();
                s->l.assign (buf.getReadPointer (0), buf.getReadPointer (0) + len);
                s->r.assign (buf.getReadPointer (1), buf.getReadPointer (1) + len);
                s->mono.resize ((size_t) len);
                for (int i = 0; i < len; ++i) s->mono[(size_t) i] = 0.5f * (s->l[(size_t) i] + s->r[(size_t) i]);
            }
        }
    }
    if (s == nullptr && ! isFx) return;   // keep the default break

    Kit kit;
    kit.sample = s;
    kit.settings.resize ((size_t) kMaxSlices);
    if (k.isValid())
    {
        kit.bpm = (double) k.getProperty ("bpm", 120.0);
        kit.bars = (int) k.getProperty ("bars", 1);
        kit.tempoFromUser = (bool) k.getProperty ("tempoFromUser", false);
        int i = 0;
        for (const auto& sv : k)
        {
            if (! sv.hasType (idSlice) || i >= kMaxSlices) continue;
            XmlElement e ("s");
            for (int p = 0; p < sv.getNumProperties(); ++p) e.setAttribute (sv.getPropertyName (p).toString(), sv.getProperty (sv.getPropertyName (p)).toString());
            kit.settings[(size_t) i++] = readSettings (e);
        }
        if (s != nullptr)
        {
            std::vector<int> m;
            for (auto& t : StringArray::fromTokens (k.getProperty ("markers").toString(), ",", "")) m.push_back (t.getIntValue());
            kit.slices = slicesFromMarkers (m, s->length());
        }
    }
    sampleName = k.getProperty ("name").toString();
    editKit = kit;
    lastSliceParams = { raw[SliceMode]->load(), raw[Sensitivity]->load(), raw[MinSlice]->load() };
    pushKit (false);
}

AudioProcessorEditor* ChopShopProcessor::createEditor() { return new ChopShopEditor (*this); }
} // namespace cs

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new cs::ChopShopProcessor(); }
