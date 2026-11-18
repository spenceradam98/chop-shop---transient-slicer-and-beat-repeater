#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include "Engine.h"

#ifndef CHOPSHOP_FX
 #define CHOPSHOP_FX 0
#endif

namespace cs
{
// Built twice: Chop Shop (instrument, CHOPSHOP_FX=0) and Chop Shop FX (effect, CHOPSHOP_FX=1).
// The message thread owns the editable kit (`kit()`); every edit makes a copy for the engine.
class ChopShopProcessor : public juce::AudioProcessor, private juce::Timer
{
public:
    static constexpr bool isFx = CHOPSHOP_FX != 0;

    ChopShopProcessor();
    ~ChopShopProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout&) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.5; }

    int getNumPrograms() override;
    int getCurrentProgram() override { return juce::jmax (0, currentProgram); }
    void setCurrentProgram (int index) override { loadFactoryPreset (index); }
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- the break (message thread)
    const Kit& kit() const { return editKit; }
    int kitVersion() const { return version; }
    juce::String getSampleName() const { return sampleName; }
    bool loadAudioFile (const juce::File&);
    void loadFactoryBreak (int index);
    void setSample (std::shared_ptr<Sample>, double bpm, int bars, bool atDownbeat);
    void reslice();
    void setTempo (double bpm);
    void setBars (int bars);
    void halveTempo (bool halve);

    // markers: editing them switches the slice mode to Manual
    void addMarker (int samplePos);
    void moveMarker (int sliceIndex, int samplePos);   // moves the start of slice sliceIndex (> 0)
    void removeMarker (int sliceIndex);

    void setSliceSettings (int slice, const SliceSettings&);
    int selectedSlice = 0;
    void audition (int slice) { auditionSlice.store (slice); }

    // FX build
    void armCapture() { engine.armCapture(); }

    // ---- export (files go to a temp folder, ready for drag and drop)
    juce::File exportSliceWav (int slice);
    juce::File exportPatternMidi (int cycles = 4);

    // ---- presets (factory pattern presets + user files)
    void loadFactoryPreset (int index);
    bool loadUserPreset (const juce::File&);
    bool saveUserPreset (const juce::String& name);
    juce::Array<juce::File> userPresets() const;
    static juce::File userPresetFolder();
    juce::String getPresetName() const { return presetName; }
    void stepPreset (int delta);

    // ---- UI state saved with the session
    int themeIndex = 0;
    float uiScale = 1.0f;

    const Engine& getEngine() const { return engine; }
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

private:
    static BusesProperties makeBuses();
    void timerCallback() override;
    void pushKit (bool atDownbeat);
    std::array<float, NumParams> currentValues() const;
    void applyValues (const std::array<float, NumParams>&);
    std::vector<int> currentMarkers() const;

    Engine engine;
    std::array<std::atomic<float>*, NumParams> raw {};
    Kit editKit;
    int version = 0;
    juce::String sampleName;
    std::array<float, 3> lastSliceParams { -1, -1, -1 };
    std::atomic<int> auditionSlice { -1 };
    std::vector<float> scratchL, scratchR;
    int maxBlock = 512;
    int currentProgram = 0;
    juce::String presetName { "Straight Chop" };
    juce::AudioFormatManager formats;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ChopShopProcessor)
};
} // namespace cs
