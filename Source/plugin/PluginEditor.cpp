#include "PluginEditor.h"
#include "Breaks.h"
#include "Presets.h"

using namespace juce;

namespace cs
{
namespace
{
constexpr int kMargin = 14, kKnob = 58, kCellW = 64;
constexpr float kStart = MathConstants<float>::pi * -0.78f, kEnd = MathConstants<float>::pi * 0.78f;
Font titleFont (float size) { return Font (FontOptions (size, Font::bold)).withExtraKerningFactor (0.12f); }
const char* kAudioExts = "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3";
}

// ============================================================================ waveform
WaveView::WaveView (ChopShopProcessor& p, ChopShopContent& o) : proc (p), owner (o)
{
    setTooltip ("Click a slice to hear it. Double-click to add a marker, drag a marker to move it, right-click a marker to delete it.");
}

int WaveView::sampleAt (float x) const
{
    const auto& k = proc.kit();
    if (k.sample == nullptr) return 0;
    return jlimit (0, k.sample->length() - 1, (int) ((x - 4.0f) / jmax (1.0f, getWidth() - 8.0f) * k.sample->length()));
}

float WaveView::xOf (double s) const
{
    const auto& k = proc.kit();
    if (k.sample == nullptr || k.sample->length() == 0) return 4.0f;
    return 4.0f + (float) (s / k.sample->length()) * (getWidth() - 8.0f);
}

int WaveView::markerNear (float x) const
{
    const auto& k = proc.kit();
    for (int i = 1; i < (int) k.slices.size(); ++i)
        if (std::abs (xOf (k.slices[(size_t) i].start) - x) < 5.0f) return i;
    return -1;
}

int WaveView::sliceAt (int s) const
{
    const auto& k = proc.kit();
    for (int i = 0; i < (int) k.slices.size(); ++i)
        if (s >= k.slices[(size_t) i].start && s < k.slices[(size_t) i].end) return i;
    return -1;
}

void WaveView::paint (Graphics& g)
{
    const auto& t = owner.theme();
    auto r = getLocalBounds().toFloat();
    g.setColour (t.waveBg);
    g.fillRoundedRectangle (r, t.corner);
    const auto& k = proc.kit();
    if (k.sample == nullptr)
    {
        g.setColour (t.textDim);
        g.setFont (titleFont (16.0f));
        g.drawText (ChopShopProcessor::isFx ? "Press CAPTURE: the next bars of the track get recorded, sliced and chopped"
                                            : "Drop a break here, press LOAD, or pick one of the BREAKS",
                    getLocalBounds(), Justification::centred);
        return;
    }

    const int w = getWidth() - 8;
    if (cacheVersion != proc.kitVersion() || (int) peaks.size() != w)
    {
        cacheVersion = proc.kitVersion();
        peaks.assign ((size_t) jmax (1, w), { 0.0f, 0.0f });
        const auto& x = k.sample->mono;
        const double per = (double) x.size() / jmax (1, w);
        for (int c = 0; c < w; ++c)
        {
            float lo = 0, hi = 0;
            const int a = (int) (c * per), b = jmin ((int) x.size(), (int) ((c + 1) * per) + 1);
            for (int i = a; i < b; ++i) { lo = jmin (lo, x[(size_t) i]); hi = jmax (hi, x[(size_t) i]); }
            peaks[(size_t) c] = { lo, hi };
        }
    }

    const float mid = r.getCentreY(), half = r.getHeight() * 0.42f;
    // slice regions
    for (int i = 0; i < (int) k.slices.size(); ++i)
    {
        const auto& s = k.slices[(size_t) i];
        const auto col = t.slices[(size_t) i % t.slices.size()];
        const float x0 = xOf (s.start), x1 = xOf (s.end);
        const bool sel = i == proc.selectedSlice;
        g.setColour (col.withAlpha (sel ? 0.34f : 0.13f));
        g.fillRect (x0, r.getY() + 2, x1 - x0, r.getHeight() - 4);
        g.setColour (col.withAlpha (0.9f));
        g.setFont (titleFont (10.0f));
        if (x1 - x0 > 14) g.drawText (String (i + 1), (int) x0 + 3, 4, 24, 12, Justification::left);
    }
    // waveform
    for (int c = 0; c < (int) peaks.size(); ++c)
    {
        const int s = (int) ((double) c / peaks.size() * k.sample->length());
        const int sl = sliceAt (s);
        g.setColour (sl == proc.selectedSlice ? t.wave : t.wave.withAlpha (0.75f));
        g.drawVerticalLine (4 + c, mid - peaks[(size_t) c].second * half, mid - peaks[(size_t) c].first * half + 1.0f);
    }
    // markers
    for (int i = 1; i < (int) k.slices.size(); ++i)
    {
        const float x = xOf (k.slices[(size_t) i].start);
        g.setColour (t.slices[(size_t) i % t.slices.size()]);
        g.drawLine (x, r.getY() + 2, x, r.getBottom() - 2, i == dragMarker ? 2.0f : 1.2f);
        Path tri; tri.addTriangle (x - 5, r.getBottom() - 2, x + 5, r.getBottom() - 2, x, r.getBottom() - 9);
        g.fillPath (tri);
    }
    // loop end (bars at the original tempo)
    const double loopEnd = k.loopBeats() * 60.0 / k.bpm * k.sample->sampleRate;
    if (loopEnd < k.sample->length() - 1)
    {
        g.setColour (t.textDim.withAlpha (0.6f));
        const float x = xOf (loopEnd);
        g.fillRect (x, r.getY(), r.getRight() - x, r.getHeight());
    }
    // play cursor
    const float cur = proc.getEngine().playCursor.load();
    if (cur >= 0)
    {
        g.setColour (Colours::white.withAlpha (0.85f));
        g.drawVerticalLine ((int) xOf (cur), r.getY() + 2, r.getBottom() - 2);
    }
}

void WaveView::mouseMove (const MouseEvent& e)
{
    setMouseCursor (markerNear ((float) e.x) > 0 ? MouseCursor::LeftRightResizeCursor : MouseCursor::PointingHandCursor);
}

void WaveView::mouseDown (const MouseEvent& e)
{
    const int m = markerNear ((float) e.x);
    if (m > 0 && (e.mods.isPopupMenu() || e.mods.isAltDown())) { proc.removeMarker (m); return; }
    if (m > 0) { dragMarker = m; return; }
    const int s = sliceAt (sampleAt ((float) e.x));
    if (s >= 0) { owner.selectSlice (s); proc.audition (s); }
}

void WaveView::mouseDrag (const MouseEvent& e)
{
    if (dragMarker > 0) proc.moveMarker (dragMarker, sampleAt ((float) e.x));
}

void WaveView::mouseDoubleClick (const MouseEvent& e)
{
    if (markerNear ((float) e.x) < 0) proc.addMarker (sampleAt ((float) e.x));
}

// ============================================================================ pads
PadGrid::PadGrid (ChopShopProcessor& p, ChopShopContent& o) : proc (p), owner (o)
{
    setTooltip ("Click a pad to select and hear its slice. Drag a pad out to drop the slice into your DAW as a WAV.");
}

Rectangle<float> PadGrid::padBounds (int i, int n) const
{
    const int perRow = n <= 16 ? jmax (1, n) : 16;
    const int rows = (n + perRow - 1) / perRow;
    const float gap = 4.0f;
    const float pw = (getWidth() - gap * (perRow - 1)) / perRow;
    const float ph = (getHeight() - gap * (rows - 1)) / jmax (1, rows);
    return { (i % perRow) * (pw + gap), (i / perRow) * (ph + gap), pw, ph };
}

int PadGrid::padAt (Point<float> p) const
{
    const int n = (int) proc.kit().slices.size();
    for (int i = 0; i < n; ++i) if (padBounds (i, n).contains (p)) return i;
    return -1;
}

void PadGrid::tick()
{
    const auto& e = proc.getEngine();
    for (int i = 0; i < kMaxSlices; ++i)
    {
        const uint32_t h = e.hits[(size_t) i].load();
        if (h != lastHits[(size_t) i]) { lastHits[(size_t) i] = h; flash[(size_t) i] = 1.0f; }
        else flash[(size_t) i] *= 0.82f;
    }
}

void PadGrid::paint (Graphics& g)
{
    const auto& t = owner.theme();
    const auto& k = proc.kit();
    const int n = (int) k.slices.size();
    const int root = roundToInt (proc.apvts.getRawParameterValue ("root_note")->load());
    for (int i = 0; i < n; ++i)
    {
        auto b = padBounds (i, n);
        const auto col = t.slices[(size_t) i % t.slices.size()];
        const bool sel = i == proc.selectedSlice;
        g.setColour (t.pad.interpolatedWith (col, 0.15f + 0.75f * flash[(size_t) i]));
        g.fillRoundedRectangle (b, t.corner * 0.6f);
        if (t.knobStyle == 2 && flash[(size_t) i] > 0.1f)
        {
            g.setColour (col.withAlpha (0.35f * flash[(size_t) i]));
            g.drawRoundedRectangle (b.expanded (2), t.corner * 0.6f, 3.0f);
        }
        g.setColour (sel ? t.accent : t.panelEdge.withAlpha (0.8f));
        g.drawRoundedRectangle (b.reduced (0.5f), t.corner * 0.6f, sel ? 2.0f : 1.0f);

        const auto& s = k.settingsFor (i);
        g.setColour (t.padText);
        g.setFont (titleFont (jmin (13.0f, b.getHeight() * 0.32f)));
        g.drawText (String (i + 1), b.reduced (4, 2).toNearestInt(), Justification::topLeft);
        if (b.getWidth() > 50 && b.getHeight() > 40)
        {
            g.setColour (t.padText.withAlpha (0.6f));
            g.setFont (Font (FontOptions (9.5f)));
            g.drawText (MidiMessage::getMidiNoteName (root + i, true, true, 3), b.reduced (4, 3).toNearestInt(), Justification::topRight);
        }
        // probability bars: play, repeat, reverse, drop
        const float vals[] = { s.prob, s.repeatProb, s.reverseProb, s.dropProb };
        const Colour cols[] = { t.padText, t.accent, t.accent2, col.darker (0.2f) };
        const float bw = jmin (6.0f, (b.getWidth() - 10) / 4.0f - 2), bh = b.getHeight() * 0.42f;
        for (int j = 0; j < 4; ++j)
        {
            auto bar = Rectangle<float> (b.getX() + 5 + j * (bw + 2), b.getBottom() - 4 - bh, bw, bh);
            g.setColour (t.padText.withAlpha (0.12f));
            g.fillRect (bar);
            g.setColour (cols[j].withAlpha (0.85f));
            g.fillRect (bar.withTop (bar.getBottom() - bh * jlimit (0.0f, 1.0f, vals[j])));
        }
        if (s.reverse)
        {
            g.setColour (t.padText.withAlpha (0.7f));
            g.setFont (Font (FontOptions (9.0f, Font::bold)));
            g.drawText ("REV", b.reduced (4, 3).toNearestInt(), Justification::bottomRight);
        }
    }
}

void PadGrid::mouseDown (const MouseEvent& e)
{
    dragPad = padAt (e.position);
    dragging = false;
    if (dragPad >= 0) { owner.selectSlice (dragPad); proc.audition (dragPad); }
}

void PadGrid::mouseDrag (const MouseEvent& e)
{
    if (dragPad < 0 || dragging || e.getDistanceFromDragStart() < 8) return;
    dragging = true;
    const auto f = proc.exportSliceWav (dragPad);
    if (f.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

void MidiDragButton::mouseDrag (const MouseEvent& e)
{
    if (dragging || e.getDistanceFromDragStart() < 8) return;
    dragging = true;
    const auto f = proc.exportPatternMidi();
    if (f.existsAsFile()) DragAndDropContainer::performExternalDragDropOfFiles ({ f.getFullPathName() }, false, this);
}

// ============================================================================ content
ChopShopContent::ChopShopContent (ChopShopProcessor& p) : proc (p), wave (p, *this), pads (p, *this), midiButton (p)
{
    setLookAndFeel (&lnf);
    addAndMakeVisible (wave);
    addAndMakeVisible (pads);

    // slicing
    addBox (modeBox, "slice_mode", modeAtt);
    addKnob ("Sensitivity", "sensitivity");
    addKnob ("Min Slice", "min_slice");
    // mapping
    addBox (voicesBox, "polyphony", voicesAtt);
    addKnob ("Root Note", "root_note");
    addToggle (chokeToggle, "choke", chokeAtt);
    chokeToggle.setTooltip ("Poly: a slice cuts its own previous hit. Mono always cuts.");
    // per slice
    sliceKnobs = {
        &addSliceKnob ("Level", -24, 6, 0.1, 0, " dB"),
        &addSliceKnob ("Pitch", -24, 24, 0.1, 0, " st"),
        &addSliceKnob ("Attack", 0, 100, 0.1, 0, " ms"),
        &addSliceKnob ("Decay", 0.05, 1, 0.01, 1, ""),
        &addSliceKnob ("Play", 0, 1, 0.01, 1, ""),
        &addSliceKnob ("Repeat", 0, 1, 0.01, 0, ""),
        &addSliceKnob ("Reverse", 0, 1, 0.01, 0, ""),
        &addSliceKnob ("Drop", 0, 1, 0.01, 0, ""),
        &addSliceKnob ("Depth", 0, 1, 0.01, 0.6, ""),
    };
    sliceKnobs[0]->slider.getProperties().set ("bipolar", true);
    sliceKnobs[1]->slider.getProperties().set ("bipolar", true);
    sliceKnobs[4]->slider.setTooltip ("Chance this slice plays at all (scaled by Mutation)");
    sliceKnobs[5]->slider.setTooltip ("Chance this slice stutters at the repeat rate");
    sliceKnobs[6]->slider.setTooltip ("Chance this slice plays backwards");
    sliceKnobs[7]->slider.setTooltip ("Chance of a tape-stop pitch drop across the step");
    sliceKnobs[8]->slider.setTooltip ("How far the pitch drop falls");
    for (int i = 0; i < NumRepeatRates; ++i) rateBox.addItem (repeatRateName (i), i + 1);
    rateBox.setJustificationType (Justification::centred);
    rateBox.setTooltip ("Stutter rate");
    rateBox.onChange = [this] { pushSliceSettings(); };
    reverseToggle.setTooltip ("Always play this slice backwards");
    reverseToggle.onClick = [this] { pushSliceSettings(); };
    addAndMakeVisible (rateBox);
    addAndMakeVisible (reverseToggle);
    // pattern
    addToggle (patOnToggle, "pat_on", patOnAtt);
    addToggle (followToggle, "follow_host", followAtt);
    addToggle (lockToggle, "lock", lockAtt);
    followToggle.setTooltip ("Run in time with the host transport. Off: runs on its own whenever the pattern is on.");
    lockToggle.setTooltip ("Every loop makes the same choices. Off: each loop rolls again (still the same for a given seed).");
    addBox (orderBox, "order", orderAtt);
    addBox (speedBox, "speed", speedAtt);
    addKnob ("Swing", "swing");
    addKnob ("Mutation", "mutation");
    addKnob ("Seed", "seed");
    diceButton.setTooltip ("Roll a new seed");
    diceButton.onClick = [this]
    {
        if (auto* prm = proc.apvts.getParameter ("seed"))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) Random::getSystemRandom().nextInt ({ 1, 10000 })));
    };
    addAndMakeVisible (diceButton);
    midiButton.setTooltip ("Drag the pattern (four loops at the current seed) into your DAW as a MIDI clip");
    addAndMakeVisible (midiButton);
    // global
    addBox (stretchBox, "stretch", stretchAtt);
    stretchBox.setTooltip ("Off: slices keep their speed. Repitch: speed follows the host tempo. Stretch: tempo follows, pitch stays.");
    addKnob ("Filter", "filter").slider.getProperties().set ("bipolar", true);
    addKnob ("Crush", "crush");
    addKnob ("Output", "output");
    if (ChopShopProcessor::isFx)
    {
        addKnob ("Mix", "mix");
        addBox (captureBarsBox, "capture_bars", captureBarsAtt);
        captureBarsBox.setTooltip ("Bars to capture");
        captureButton.setTooltip ("Record the next bars of the track (from the next downbeat), then slice them");
        captureButton.onClick = [this] { proc.armCapture(); };
        addAndMakeVisible (captureButton);
    }
    else
    {
        loadButton.setTooltip ("Load a break (WAV, AIFF, FLAC, OGG, MP3), or drop one anywhere on the window");
        loadButton.onClick = [this] { chooseFile(); };
        breaksButton.setTooltip ("Demo breaks made for Chop Shop");
        breaksButton.onClick = [this] { showBreaksMenu(); };
        addAndMakeVisible (loadButton);
        addAndMakeVisible (breaksButton);
    }

    // tempo of the break
    for (auto* l : { &bpmLabel, &barsLabel })
    {
        l->setEditable (true);
        l->setJustificationType (Justification::centred);
        addAndMakeVisible (*l);
    }
    bpmLabel.setTooltip ("Original tempo of the break (click to type)");
    barsLabel.setTooltip ("Length in bars (click to type)");
    bpmLabel.onTextChange = [this] { const double v = bpmLabel.getText().retainCharacters ("0123456789.").getDoubleValue(); if (v > 0) proc.setTempo (v); shownVersion = -1; };
    barsLabel.onTextChange = [this] { const int v = barsLabel.getText().retainCharacters ("0123456789").getIntValue(); if (v > 0) proc.setBars (v); shownVersion = -1; };
    halfButton.setTooltip ("Halve the tempo");
    doubleButton.setTooltip ("Double the tempo");
    halfButton.onClick = [this] { proc.halveTempo (true); };
    doubleButton.onClick = [this] { proc.halveTempo (false); };
    addAndMakeVisible (halfButton);
    addAndMakeVisible (doubleButton);
    statusLabel.setJustificationType (Justification::centredLeft);
    addAndMakeVisible (statusLabel);

    presetButton.onClick = [this] { showPresetMenu(); };
    prevButton.onClick = [this] { proc.stepPreset (-1); };
    nextButton.onClick = [this] { proc.stepPreset (1); };
    saveButton.onClick = [this] { savePresetDialog(); };
    presetButton.setTooltip ("Pattern presets");
    saveButton.setTooltip ("Save the pattern and settings as a user preset");
    for (auto* b : { &presetButton, &prevButton, &nextButton, &saveButton }) addAndMakeVisible (*b);

    for (int i = 0; i < (int) themes().size(); ++i) themeBox.addItem (themes()[(size_t) i].name, i + 1);
    themeBox.onChange = [this] { applyTheme (themeBox.getSelectedId() - 1); };
    addAndMakeVisible (themeBox);

    setSize (W, H);
    applyTheme (jlimit (0, (int) themes().size() - 1, proc.themeIndex));
    themeBox.setSelectedId (proc.themeIndex + 1, dontSendNotification);
    refreshSliceControls();
    startTimerHz (30);
}

ChopShopContent::~ChopShopContent()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

ChopShopContent::Knob& ChopShopContent::addKnob (const String& label, const char* paramId)
{
    auto k = std::make_unique<Knob>();
    k->label = label;
    k->slider.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (Slider::NoTextBox, false, 0, 0);
    k->slider.setRotaryParameters (MathConstants<float>::twoPi + kStart, MathConstants<float>::twoPi + kEnd, true);
    k->slider.setPopupDisplayEnabled (true, true, this, 1200);
    addAndMakeVisible (k->slider);
    if (paramId != nullptr)
    {
        k->attach = std::make_unique<AudioProcessorValueTreeState::SliderAttachment> (proc.apvts, paramId, k->slider);
        if (auto* prm = proc.apvts.getParameter (paramId))
        {
            k->slider.setDoubleClickReturnValue (true, prm->convertFrom0to1 (prm->getDefaultValue()));
            k->slider.setTooltip (prm->getName (64));
        }
    }
    knobs.push_back (std::move (k));
    return *knobs.back();
}

ChopShopContent::Knob& ChopShopContent::addSliceKnob (const String& label, double lo, double hi, double step, double def, const String& suffix)
{
    auto& k = addKnob (label, nullptr);
    k.slider.setRange (lo, hi, step);
    k.slider.setValue (def, dontSendNotification);
    k.slider.setDoubleClickReturnValue (true, def);
    if (suffix.isNotEmpty()) k.slider.setTextValueSuffix (suffix);
    else k.slider.textFromValueFunction = [] (double v) { return String (roundToInt (v * 100.0)) + "%"; };
    if (k.slider.getTooltip().isEmpty()) k.slider.setTooltip (label + " (selected slice)");
    k.slider.onValueChange = [this] { pushSliceSettings(); };
    return k;
}

void ChopShopContent::addBox (ComboBox& box, const char* paramId, std::unique_ptr<AudioProcessorValueTreeState::ComboBoxAttachment>& att)
{
    const int idx = paramIndex (paramId);
    box.addItemList (StringArray::fromTokens (paramDefs()[idx].choices, "|", ""), 1);
    box.setJustificationType (Justification::centred);
    box.setTooltip (paramDefs()[idx].name);
    addAndMakeVisible (box);
    att = std::make_unique<AudioProcessorValueTreeState::ComboBoxAttachment> (proc.apvts, paramId, box);
}

void ChopShopContent::addToggle (ToggleButton& b, const char* paramId, std::unique_ptr<AudioProcessorValueTreeState::ButtonAttachment>& att)
{
    addAndMakeVisible (b);
    att = std::make_unique<AudioProcessorValueTreeState::ButtonAttachment> (proc.apvts, paramId, b);
}

void ChopShopContent::placeKnob (Knob& k, int x, int y) { k.slider.setBounds (x + (kCellW - kKnob) / 2, y, kKnob, kKnob); }

void ChopShopContent::resized()
{
    // header
    const int hy = 14, hh = 30;
    prevButton.setBounds (300, hy, 28, hh);
    presetButton.setBounds (332, hy, 250, hh);
    nextButton.setBounds (586, hy, 28, hh);
    saveButton.setBounds (618, hy, 58, hh);
    themeBox.setBounds (690, hy + 2, 130, hh - 4);
    if (ChopShopProcessor::isFx)
    {
        captureButton.setBounds (W - kMargin - 186, hy, 106, hh);
        captureBarsBox.setBounds (W - kMargin - 74, hy + 2, 74, hh - 4);
    }
    else
    {
        loadButton.setBounds (W - kMargin - 176, hy, 80, hh);
        breaksButton.setBounds (W - kMargin - 90, hy, 90, hh);
    }

    // waveform header row: status | bpm /2 x2 | bars
    const int ry = 58;
    statusLabel.setBounds (kMargin + 330, ry, 360, 24);
    halfButton.setBounds (W - kMargin - 286, ry, 32, 24);
    bpmLabel.setBounds (W - kMargin - 250, ry, 90, 24);
    doubleButton.setBounds (W - kMargin - 156, ry, 32, 24);
    barsLabel.setBounds (W - kMargin - 90, ry, 90, 24);

    wave.setBounds (kMargin, 88, W - 2 * kMargin, 170);
    pads.setBounds (kMargin, 266, W - 2 * kMargin, 108);

    // panels
    panels.clear();
    const int py = 386, ph = H - py - kMargin;
    int x = kMargin;
    auto panel = [&] (const String& title, int w) { panels.push_back ({ title, { x, py, w, ph } }); x += w + 10; return panels.back().bounds; };

    auto knobAt = [this] (const char* label) -> Knob*
    {
        for (auto& k : knobs) if (k->label == label && k->attach != nullptr) return k.get();
        return nullptr;
    };

    auto b = panel ("SLICING", 160);
    modeBox.setBounds (b.getX() + 10, b.getY() + 32, b.getWidth() - 20, 24);
    placeKnob (*knobAt ("Sensitivity"), b.getX() + 12, b.getY() + 74);
    placeKnob (*knobAt ("Min Slice"), b.getX() + 84, b.getY() + 74);

    b = panel ("MAPPING", 140);
    voicesBox.setBounds (b.getX() + 10, b.getY() + 32, b.getWidth() - 20, 24);
    placeKnob (*knobAt ("Root Note"), b.getX() + 6, b.getY() + 74);
    chokeToggle.setBounds (b.getX() + 72, b.getY() + 92, 66, 22);

    b = panel ("SLICE", 330);
    reverseToggle.setBounds (b.getX() + 10, b.getY() + 32, 100, 24);
    rateBox.setBounds (b.getRight() - 120, b.getY() + 32, 110, 24);
    for (int i = 0; i < 4; ++i) placeKnob (*sliceKnobs[(size_t) i], b.getX() + 6 + i * kCellW, b.getY() + 74);
    for (int i = 0; i < 5; ++i) placeKnob (*sliceKnobs[(size_t) (4 + i)], b.getX() + 6 + i * kCellW, b.getY() + 170);

    b = panel ("PATTERN", 250);
    patOnToggle.setBounds (b.getX() + 10, b.getY() + 32, 80, 24);
    followToggle.setBounds (b.getX() + 90, b.getY() + 32, 100, 24);
    lockToggle.setBounds (b.getX() + 190, b.getY() + 32, 56, 24);
    orderBox.setBounds (b.getX() + 10, b.getY() + 64, 130, 24);
    speedBox.setBounds (b.getX() + 146, b.getY() + 64, 94, 24);
    placeKnob (*knobAt ("Swing"), b.getX() + 6, b.getY() + 102);
    placeKnob (*knobAt ("Mutation"), b.getX() + 6 + kCellW, b.getY() + 102);
    placeKnob (*knobAt ("Seed"), b.getX() + 6 + 2 * kCellW, b.getY() + 102);
    diceButton.setBounds (b.getX() + 6 + 3 * kCellW, b.getY() + 118, 46, 26);
    midiButton.setBounds (b.getX() + 10, b.getBottom() - 46, b.getWidth() - 20, 32);

    b = panel ("GLOBAL", W - kMargin - x);
    stretchBox.setBounds (b.getX() + 10, b.getY() + 32, b.getWidth() - 20, 24);
    placeKnob (*knobAt ("Filter"), b.getX() + 14, b.getY() + 74);
    placeKnob (*knobAt ("Crush"), b.getX() + 14 + kCellW + 6, b.getY() + 74);
    placeKnob (*knobAt ("Output"), b.getX() + 14, b.getY() + 170);
    if (auto* mix = knobAt ("Mix")) placeKnob (*mix, b.getX() + 14 + kCellW + 6, b.getY() + 170);
}

void ChopShopContent::applyTheme (int index)
{
    index = jlimit (0, (int) themes().size() - 1, index);
    proc.themeIndex = index;
    lnf.setTheme (themes()[(size_t) index]);
    background = Image (Image::ARGB, W * 2, H * 2, true);
    Graphics g (background);
    g.addTransform (AffineTransform::scale (2.0f));
    lnf.paintBackdrop (g, W, H);
    for (auto* l : { &bpmLabel, &barsLabel, &statusLabel }) l->setColour (Label::textColourId, lnf.theme().panel.getPerceivedBrightness() > 0.5f && lnf.theme().bgTop.getPerceivedBrightness() < 0.5f ? lnf.theme().panel : lnf.theme().text);
    sendLookAndFeelChange();
    repaint();
}

void ChopShopContent::paint (Graphics& g)
{
    const auto& t = lnf.theme();
    g.drawImage (background, getLocalBounds().toFloat());
    const bool darkBg = t.bgTop.getPerceivedBrightness() < 0.5f;
    const auto onBg = darkBg ? (t.panel.getPerceivedBrightness() > 0.5f ? t.panel : t.text) : t.text;

    g.setColour (onBg);
    g.setFont (titleFont (26.0f));
    g.drawText (ChopShopProcessor::isFx ? "CHOP SHOP FX" : "CHOP SHOP", kMargin, 12, 280, 34, Justification::centredLeft);
    if (t.knobStyle == 2)
    {
        g.setColour (t.accent.withAlpha (0.25f));
        g.drawText (ChopShopProcessor::isFx ? "CHOP SHOP FX" : "CHOP SHOP", kMargin - 1, 11, 280, 34, Justification::centredLeft);
    }

    // waveform header text
    const auto& k = proc.kit();
    g.setFont (titleFont (13.0f));
    g.setColour (onBg);
    String info = proc.getSampleName().isNotEmpty() ? proc.getSampleName() : String ("No audio");
    if (k.sample != nullptr) info << "   |   " << (int) k.slices.size() << " slices   |   " << String (k.sample->length() / k.sample->sampleRate, 2) << " s";
    g.drawText (info, kMargin + 2, 58, 330, 24, Justification::centredLeft);
    g.setFont (Font (FontOptions (11.0f, Font::bold)));
    g.drawText ("BPM", W - kMargin - 326, 58, 40, 24, Justification::centredRight);

    // panels
    for (const auto& p : panels)
    {
        auto r = p.bounds.toFloat();
        g.setColour (Colours::black.withAlpha (0.25f));
        g.fillRoundedRectangle (r.translated (0, 3), t.corner);
        g.setColour (t.panel);
        g.fillRoundedRectangle (r, t.corner);
        g.setColour (t.panelEdge);
        g.drawRoundedRectangle (r.reduced (0.5f), t.corner, 1.2f);
        String title = p.title;
        if (title == "SLICE") title << " " << (proc.selectedSlice + 1) << "  (" << MidiMessage::getMidiNoteName (roundToInt (proc.apvts.getRawParameterValue ("root_note")->load()) + proc.selectedSlice, true, true, 3) << ")";
        g.setColour (t.accent);
        g.setFont (titleFont (13.0f));
        g.drawText (title, p.bounds.getX() + 10, p.bounds.getY() + 6, p.bounds.getWidth() - 20, 20, Justification::centredLeft);
        g.setColour (t.panelEdge.withAlpha (0.5f));
        g.drawHorizontalLine (p.bounds.getY() + 27, r.getX() + 8, r.getRight() - 8);
    }

    // knob labels and values
    for (auto& kn : knobs)
    {
        if (! kn->slider.isVisible() || kn->slider.getWidth() == 0) continue;
        auto b = kn->slider.getBounds();
        g.setColour (t.text);
        g.setFont (Font (FontOptions (11.5f, Font::bold)));
        g.drawText (kn->label, b.getX() - 8, b.getBottom() + 1, b.getWidth() + 16, 14, Justification::centred);
        g.setColour (t.textDim);
        g.setFont (Font (FontOptions (10.5f)));
        g.drawText (kn->slider.getTextFromValue (kn->slider.getValue()), b.getX() - 8, b.getBottom() + 15, b.getWidth() + 16, 13, Justification::centred);
    }
    g.setColour (t.textDim);
    g.setFont (Font (FontOptions (10.5f)));
    for (const auto& p : panels)
        if (p.title == "SLICE")
            g.drawText ("Repeat rate", p.bounds.getRight() - 200, p.bounds.getY() + 32, 76, 24, Justification::centredRight);
}

void ChopShopContent::selectSlice (int i)
{
    proc.selectedSlice = jlimit (0, jmax (0, (int) proc.kit().slices.size() - 1), i);
    refreshSliceControls();
    repaint();
}

void ChopShopContent::refreshSliceControls()
{
    updatingSlice = true;
    const auto& s = proc.kit().settingsFor (proc.selectedSlice);
    const float vals[] = { s.level, s.pitch, s.attack, s.decay, s.prob, s.repeatProb, s.reverseProb, s.dropProb, s.dropDepth };
    for (size_t i = 0; i < sliceKnobs.size(); ++i) sliceKnobs[i]->slider.setValue (vals[i], dontSendNotification);
    rateBox.setSelectedId (s.repeatRate + 1, dontSendNotification);
    reverseToggle.setToggleState (s.reverse, dontSendNotification);
    shownSlice = proc.selectedSlice;
    updatingSlice = false;
}

void ChopShopContent::pushSliceSettings()
{
    if (updatingSlice) return;
    SliceSettings s = proc.kit().settingsFor (proc.selectedSlice);
    s.level = (float) sliceKnobs[0]->slider.getValue();
    s.pitch = (float) sliceKnobs[1]->slider.getValue();
    s.attack = (float) sliceKnobs[2]->slider.getValue();
    s.decay = (float) sliceKnobs[3]->slider.getValue();
    s.prob = (float) sliceKnobs[4]->slider.getValue();
    s.repeatProb = (float) sliceKnobs[5]->slider.getValue();
    s.reverseProb = (float) sliceKnobs[6]->slider.getValue();
    s.dropProb = (float) sliceKnobs[7]->slider.getValue();
    s.dropDepth = (float) sliceKnobs[8]->slider.getValue();
    s.repeatRate = jmax (0, rateBox.getSelectedId() - 1);
    s.reverse = reverseToggle.getToggleState();
    proc.setSliceSettings (proc.selectedSlice, s);
    shownVersion = proc.kitVersion();
    pads.repaint();
}

// ---------------------------------------------------------------- files, presets, breaks
bool ChopShopContent::isInterestedInFileDrag (const StringArray& files)
{
    for (auto& f : files)
        if (File (f).hasFileExtension (kAudioExts)) return true;
    return false;
}

void ChopShopContent::filesDropped (const StringArray& files, int, int)
{
    for (auto& f : files)
        if (File (f).hasFileExtension (kAudioExts) && proc.loadAudioFile (File (f))) break;
}

void ChopShopContent::chooseFile()
{
    chooser = std::make_unique<FileChooser> ("Load a break", File::getSpecialLocation (File::userMusicDirectory), kAudioExts);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [this] (const FileChooser& fc)
    {
        const auto f = fc.getResult();
        if (f.existsAsFile() && ! proc.loadAudioFile (f))
            AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Chop Shop", "Could not read " + f.getFileName());
    });
}

void ChopShopContent::showBreaksMenu()
{
    PopupMenu m;
    m.addSectionHeader ("Demo breaks (made for Chop Shop)");
    const auto& br = factoryBreaks();
    for (int i = 0; i < (int) br.size(); ++i)
        m.addItem (i + 1, String (br[(size_t) i].name) + "   " + String ((int) br[(size_t) i].bpm) + " bpm");
    m.showMenuAsync (PopupMenu::Options().withTargetComponent (&breaksButton), [this] (int r) { if (r > 0) proc.loadFactoryBreak (r - 1); });
}

void ChopShopContent::showPresetMenu()
{
    PopupMenu menu;
    const auto& ps = patternPresets();
    menu.addSectionHeader ("Pattern presets");
    for (size_t i = 0; i < ps.size(); ++i)
        menu.addItem ((int) i + 1, ps[i].name, true, (int) i == proc.getCurrentProgram() && proc.getPresetName() == String (ps[i].name));
    const auto users = proc.userPresets();
    PopupMenu userMenu;
    for (int i = 0; i < users.size(); ++i) userMenu.addItem (10000 + i, users[i].getFileNameWithoutExtension());
    if (users.isEmpty()) userMenu.addItem (-1, "(none saved yet)", false);
    menu.addSubMenu ("User", userMenu);
    menu.addSeparator();
    menu.addItem (20000, "Save current as...");
    menu.addItem (20001, "Show preset folder");
    menu.showMenuAsync (PopupMenu::Options().withTargetComponent (&presetButton).withMinimumWidth (presetButton.getWidth()),
                        [this, users] (int r)
                        {
                            if (r <= 0) return;
                            if (r < 10000) proc.loadFactoryPreset (r - 1);
                            else if (r < 20000) proc.loadUserPreset (users[r - 10000]);
                            else if (r == 20000) savePresetDialog();
                            else
                            {
                                ChopShopProcessor::userPresetFolder().createDirectory();
                                ChopShopProcessor::userPresetFolder().revealToUser();
                            }
                        });
}

void ChopShopContent::savePresetDialog()
{
    saveWindow = std::make_unique<AlertWindow> ("Save preset", "Name for this preset:", MessageBoxIconType::NoIcon, this);
    saveWindow->setLookAndFeel (&lnf);
    saveWindow->addTextEditor ("name", proc.getPresetName());
    saveWindow->addButton ("Save", 1, KeyPress (KeyPress::returnKey));
    saveWindow->addButton ("Cancel", 0, KeyPress (KeyPress::escapeKey));
    saveWindow->enterModalState (true, ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveWindow != nullptr) proc.saveUserPreset (saveWindow->getTextEditorContents ("name"));
        saveWindow.reset();
    }), false);
}

// ---------------------------------------------------------------- live updates
void ChopShopContent::timerCallback()
{
    pads.tick();
    if (shownPreset != proc.getPresetName())
    {
        shownPreset = proc.getPresetName();
        presetButton.setButtonText (shownPreset);
    }
    if (shownVersion != proc.kitVersion() || shownSlice != proc.selectedSlice)
    {
        shownVersion = proc.kitVersion();
        const auto& k = proc.kit();
        if (! bpmLabel.isBeingEdited()) bpmLabel.setText (k.sample ? String (k.bpm, 2) : String ("-"), dontSendNotification);
        if (! barsLabel.isBeingEdited()) barsLabel.setText (k.sample ? String (k.bars) + (k.bars == 1 ? " bar" : " bars") : String ("-"), dontSendNotification);
        refreshSliceControls();
        repaint();
    }
    if (ChopShopProcessor::isFx)
    {
        const int st = proc.getEngine().captureStatus.load();
        String s;
        if (st == 1) s = "Waiting for the downbeat...";
        else if (st == 2) s = "Capturing  " + String (roundToInt (proc.getEngine().captureProgress.load() * 100.0f)) + "%";
        else if (proc.getEngine().hasPendingKit()) s = "Captured: swaps in on the next loop";
        statusLabel.setText (s, dontSendNotification);
        captureButton.setToggleState (st == 1 || st == 2, dontSendNotification);
    }
    wave.repaint();
    pads.repaint();
}

// ============================================================================ editor
ChopShopEditor::ChopShopEditor (ChopShopProcessor& p) : AudioProcessorEditor (p), proc (p), content (p)
{
    addAndMakeVisible (content);
    const double ratio = (double) ChopShopContent::W / ChopShopContent::H;
    setResizable (true, true);
    setResizeLimits ((int) (ChopShopContent::W * 0.7), (int) (ChopShopContent::H * 0.7), ChopShopContent::W * 2, ChopShopContent::H * 2);
    getConstrainer()->setFixedAspectRatio (ratio);
    const float s = jlimit (0.7f, 2.0f, proc.uiScale);
    setSize (roundToInt (ChopShopContent::W * s), roundToInt (ChopShopContent::H * s));
}

void ChopShopEditor::resized()
{
    const float s = jmin ((float) getWidth() / (float) ChopShopContent::W, (float) getHeight() / (float) ChopShopContent::H);
    content.setTransform (AffineTransform::scale (s).translated ((getWidth() - ChopShopContent::W * s) * 0.5f, 0.0f));
    content.setBounds (0, 0, ChopShopContent::W, ChopShopContent::H);
    proc.uiScale = s;
}
} // namespace cs
