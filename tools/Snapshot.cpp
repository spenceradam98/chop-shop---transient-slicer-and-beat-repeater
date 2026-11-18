// Renders the editor to PNGs (one per theme) for the docs, smoke-tests the processor
// (state round trip with the embedded audio, MIDI export, slice export), and, if paths
// to built VST3 bundles are given, loads each one like a host would.
#include "../Source/plugin/PluginEditor.h"

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    int failures = 0;
    auto check = [&] (bool ok, const juce::String& what) { std::printf ("%s  %s\n", ok ? "ok  " : "FAIL", what.toRawUTF8()); if (! ok) ++failures; };

    cs::ChopShopProcessor proc;
    proc.setPlayConfigDetails (0, 2, 48000.0, 512);
    proc.prepareToPlay (48000.0, 512);
    proc.loadFactoryBreak (6);
    proc.loadFactoryPreset (1);   // Jungle Mutate
    proc.selectedSlice = 4;

    // play a little so pads flash and the cursor shows
    juce::AudioBuffer<float> buf (2, 512);
    juce::MidiBuffer midi;
    double peak = 0;
    for (int b = 0; b < 120; ++b)
    {
        proc.processBlock (buf, midi);
        peak = juce::jmax (peak, (double) buf.getMagnitude (0, 512));
    }
    check (peak > 0.05, "pattern plays in the standalone (free-running clock)");

    for (int t = 0; t < (int) cs::themes().size(); ++t)
    {
        proc.themeIndex = t;
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setSize (cs::ChopShopContent::W, cs::ChopShopContent::H);
        if (auto* e = dynamic_cast<cs::ChopShopEditor*> (ed.get())) e->getContent().refreshNow();
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 2.0f);
        auto name = cs::themes()[(size_t) t].name.toLowerCase().replaceCharacter (' ', '-');
        juce::FileOutputStream os (outDir.getChildFile ("theme-" + name + ".png"));
        os.setPosition (0); os.truncate();
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    // state round trip keeps the audio, markers and per-slice settings
    {
        proc.addMarker (proc.kit().slices[2].start + 3000);
        auto s = proc.kit().settingsFor (3); s.repeatProb = 0.77f; proc.setSliceSettings (3, s);
        juce::MemoryBlock state;
        proc.getStateInformation (state);
        cs::ChopShopProcessor p2;
        p2.setStateInformation (state.getData(), (int) state.getSize());
        check (p2.kit().sample != nullptr && p2.kit().sample->length() == proc.kit().sample->length(), "audio restored from state");
        check (p2.kit().slices.size() == proc.kit().slices.size(), "markers restored (" + juce::String ((int) p2.kit().slices.size()) + " slices)");
        check (std::abs (p2.kit().settingsFor (3).repeatProb - 0.77f) < 1e-4f, "per-slice settings restored");
        check (std::abs (p2.kit().bpm - proc.kit().bpm) < 1e-6, "tempo restored");
        double maxErr = 0;
        for (int i = 0; i < proc.kit().sample->length(); i += 7) maxErr = juce::jmax (maxErr, (double) std::abs (p2.kit().sample->l[(size_t) i] - proc.kit().sample->l[(size_t) i]));
        check (maxErr < 1e-4, "embedded audio is lossless at 24-bit (max err " + juce::String (maxErr, 7) + ")");
        std::printf ("      state size %d KB\n", (int) state.getSize() / 1024);
    }
    {
        const auto f = proc.exportPatternMidi();
        juce::MidiFile mf;
        juce::FileInputStream in (f);
        check (in.openedOk() && mf.readFrom (in) && mf.getNumTracks() == 1 && mf.getTrack (0)->getNumEvents() > 20, "MIDI pattern export " + f.getFileName());
        const auto w = proc.exportSliceWav (2);
        check (w.getSize() > 1000, "slice WAV export " + w.getFileName());
    }

    juce::VST3PluginFormat vst3;
    for (int i = 2; i < argc; ++i)
    {
        juce::OwnedArray<juce::PluginDescription> found;
        vst3.findAllTypesForFile (found, argv[i]);
        check (found.size() == 1, "found plugin in " + juce::String (argv[i]));
        if (found.isEmpty()) continue;
        juce::String err;
        auto inst = vst3.createInstanceFromDescription (*found[0], 48000.0, 512, err);
        check (inst != nullptr, "instantiated " + found[0]->name + " " + err);
        if (inst == nullptr) continue;
        std::printf ("      %s: instrument=%d inputs=%d outputs=%d midiIn=%d\n", found[0]->name.toRawUTF8(), (int) found[0]->isInstrument,
                     inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels(), (int) inst->acceptsMidi());
        inst->prepareToPlay (48000.0, 512);
        juce::AudioBuffer<float> b (juce::jmax (2, inst->getTotalNumInputChannels(), inst->getTotalNumOutputChannels()), 512);
        juce::MidiBuffer m;
        double pk = 0;
        for (int k = 0; k < 60; ++k)
        {
            for (int c = 0; c < inst->getTotalNumInputChannels(); ++c)
                for (int s = 0; s < 512; ++s) b.setSample (c, s, 0.2f * std::sin ((k * 512 + s) * 0.03f));
            m.clear();
            if (k == 0) m.addEvent (juce::MidiMessage::noteOn (1, 38, (juce::uint8) 100), 0);
            inst->processBlock (b, m);
            pk = juce::jmax (pk, (double) b.getMagnitude (0, 512));
        }
        check (pk > 0.01, found[0]->name + " makes sound (peak " + juce::String (pk, 3) + ")");
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr, "editor opens");
        ed.reset();
        inst->releaseResources();
    }
    return failures == 0 ? 0 : 1;
}
