#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Themes.h"

namespace cs
{
class ChopShopContent;

// Waveform with coloured slice regions, draggable markers and the play cursor.
class WaveView : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveView (ChopShopProcessor&, ChopShopContent&);
    void paint (juce::Graphics&) override;
    void resized() override { cacheVersion = -1; }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override { dragMarker = -1; }
    void mouseDoubleClick (const juce::MouseEvent&) override;

private:
    int sampleAt (float x) const;
    float xOf (double sample) const;
    int markerNear (float x) const;   // slice index whose start marker is near x (> 0), or -1
    int sliceAt (int sample) const;

    ChopShopProcessor& proc;
    ChopShopContent& owner;
    std::vector<std::pair<float, float>> peaks;
    int cacheVersion = -1;
    int dragMarker = -1;
};

// One pad per slice: lights up as it plays, shows its probabilities, drags out as a WAV.
class PadGrid : public juce::Component, public juce::SettableTooltipClient
{
public:
    PadGrid (ChopShopProcessor&, ChopShopContent&);
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void tick();

private:
    juce::Rectangle<float> padBounds (int i, int n) const;
    int padAt (juce::Point<float>) const;

    ChopShopProcessor& proc;
    ChopShopContent& owner;
    std::array<uint32_t, kMaxSlices> lastHits {};
    std::array<float, kMaxSlices> flash {};
    int dragPad = -1;
    bool dragging = false;
};

// A button that drags the current pattern out as a .mid file.
class MidiDragButton : public juce::TextButton
{
public:
    explicit MidiDragButton (ChopShopProcessor& p) : juce::TextButton ("DRAG MIDI"), proc (p) {}
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent& e) override { dragging = false; juce::TextButton::mouseUp (e); }

private:
    ChopShopProcessor& proc;
    bool dragging = false;
};

class ChopShopContent : public juce::Component, public juce::FileDragAndDropTarget, private juce::Timer
{
public:
    static constexpr int W = 1120, H = 700;

    explicit ChopShopContent (ChopShopProcessor&);
    ~ChopShopContent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void filesDropped (const juce::StringArray&, int, int) override;

    const Theme& theme() const { return lnf.theme(); }
    void selectSlice (int i);
    void refreshNow() { timerCallback(); }

private:
    struct Knob
    {
        juce::String label;
        juce::Slider slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attach;
    };
    struct Panel { juce::String title; juce::Rectangle<int> bounds; };

    Knob& addKnob (const juce::String& label, const char* paramId);
    Knob& addSliceKnob (const juce::String& label, double lo, double hi, double step, double def, const juce::String& suffix);
    void addBox (juce::ComboBox&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>&);
    void addToggle (juce::ToggleButton&, const char* paramId, std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment>&);
    void placeKnob (Knob&, int x, int y);
    void applyTheme (int index);
    void showPresetMenu();
    void showBreaksMenu();
    void savePresetDialog();
    void chooseFile();
    void pushSliceSettings();
    void refreshSliceControls();
    void timerCallback() override;

    ChopShopProcessor& proc;
    ShopLookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };
    WaveView wave;
    PadGrid pads;
    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<Knob*> sliceKnobs;   // level, pitch, attack, decay, play, repeat, reverse, drop, depth
    std::vector<Panel> panels;

    juce::ComboBox modeBox, voicesBox, orderBox, speedBox, stretchBox, captureBarsBox, rateBox, themeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modeAtt, voicesAtt, orderAtt, speedAtt, stretchAtt, captureBarsAtt;
    juce::ToggleButton chokeToggle { "Choke" }, patOnToggle { "Pattern" }, followToggle { "Follow host" }, lockToggle { "Lock" }, reverseToggle { "Reverse" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> chokeAtt, patOnAtt, followAtt, lockAtt;
    juce::TextButton presetButton, prevButton { "<" }, nextButton { ">" }, saveButton { "SAVE" };
    juce::TextButton loadButton { "LOAD" }, breaksButton { "BREAKS" }, captureButton { "CAPTURE" }, diceButton { "NEW" };
    juce::TextButton halfButton { "/2" }, doubleButton { "x2" };
    MidiDragButton midiButton;
    juce::Label bpmLabel, barsLabel, statusLabel;

    juce::Image background;
    juce::String shownPreset;
    int shownVersion = -1, shownSlice = -1;
    bool updatingSlice = false;
    std::unique_ptr<juce::AlertWindow> saveWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopContent)
};

class ChopShopEditor : public juce::AudioProcessorEditor
{
public:
    explicit ChopShopEditor (ChopShopProcessor&);
    void resized() override;
    void paint (juce::Graphics& g) override { g.fillAll (juce::Colours::black); }
    ChopShopContent& getContent() { return content; }

private:
    ChopShopProcessor& proc;
    ChopShopContent content;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopEditor)
};
} // namespace cs
