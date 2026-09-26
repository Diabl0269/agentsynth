// ModuleComponent audio-file drag-and-drop tests: Sampler chrome/load path, Wavetable file drag.

#include "AudioEngine/AudioEngine.h"
#include "ModuleComponentTestFixture.h"

#include "Modules/OscillatorModule.h"
#include "Modules/SamplerModule.h"
#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Sampler chrome: the module gets a waveform view, a "Load Sample..." button and a file-name label
// on top of the usual auto-UI. The height is asserted exactly because GraphEditor's
// estimateModuleSize("Sampler") hard-codes it for the library drag ghost — if the auto-UI grows a
// row, this test fails and both numbers get updated together.
TEST_F(ModuleComponentTest, SamplerHasLoadButtonWaveformAndKnownHeight) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::TextButton* loadButton = nullptr;
    bool foundWaveform = false;
    bool foundNameLabel = false;
    for (auto* child : moduleComponent.getChildren()) {
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getButtonText().startsWith("Load Sample"))
            loadButton = button;
        if (dynamic_cast<SampleWaveformComponent*>(child) != nullptr)
            foundWaveform = true;
        if (auto* label = dynamic_cast<juce::Label*>(child); label != nullptr && label->getText() == "(no sample)")
            foundNameLabel = true;
    }

    ASSERT_NE(loadButton, nullptr) << "the Sampler needs a way to pick a file";
    EXPECT_TRUE(foundWaveform);
    EXPECT_TRUE(foundNameLabel) << "an empty Sampler should say so rather than showing a blank label";

    EXPECT_EQ(moduleComponent.getWidth(), 280);
    EXPECT_EQ(moduleComponent.getHeight(), 645)
        << "keep estimateModuleSize(\"Sampler\") in GraphEditor.cpp in sync with this";

    EXPECT_NO_THROW(moduleComponent.timerCallback());
}

TEST_F(ModuleComponentTest, NonSamplerModulesGetNoSamplerChrome) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    for (auto* child : moduleComponent.getChildren())
        EXPECT_EQ(dynamic_cast<SampleWaveformComponent*>(child), nullptr);
}

// --- Audio-file drag and drop -----------------------------------------------------------------

TEST_F(ModuleComponentTest, SamplerAcceptsAudioFileDropAndLoadsIt) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    processor.prepareToPlay(44100.0, 512);
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // A real, readable wav so the drop exercises the actual load path.
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("drop-test-146.wav");
    file.deleteFile();
    {
        juce::AudioBuffer<float> audio(1, 1024);
        audio.clear();
        for (int i = 0; i < 1024; ++i)
            audio.setSample(0, i, 0.25f);

        juce::WavAudioFormat wavFormat;
        std::unique_ptr<juce::FileOutputStream> stream(file.createOutputStream());
        ASSERT_NE(stream, nullptr);
        std::unique_ptr<juce::AudioFormatWriter> writer(wavFormat.createWriterFor(stream.get(), 44100.0, 1, 32, {}, 0));
        ASSERT_NE(writer, nullptr);
        stream.release();
        writer->writeFromAudioSampleBuffer(audio, 0, 1024);
    }

    juce::StringArray dropped{file.getFullPathName()};
    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag(dropped));

    moduleComponent.fileDragEnter(dropped, 10, 10);
    EXPECT_TRUE(moduleComponent.isFileDragHighlighted());

    moduleComponent.filesDropped(dropped, 10, 10);
    EXPECT_FALSE(moduleComponent.isFileDragHighlighted()) << "the highlight must clear on drop";
    EXPECT_EQ(processor.getSampleFilePath(), file.getFullPathName());
    ASSERT_NE(processor.getSample(), nullptr);

    file.deleteFile();
}

TEST_F(ModuleComponentTest, SamplerIgnoresNonAudioFileDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    SamplerModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::StringArray notAudio{"/tmp/patch.json", "/tmp/readme.md"};
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag(notAudio));
}

TEST_F(ModuleComponentTest, NonSamplerModuleRefusesFileDragSoItFallsThroughToTheCanvas) {
    AudioEngine engine;
    GraphEditor editor(engine);
    OscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    // Returning false is what lets JUCE keep walking up to GraphEditor, which creates a new Sampler.
    juce::StringArray dropped{"/tmp/kick.wav"};
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag(dropped));
    EXPECT_FALSE(moduleComponent.isFileDragHighlighted());
}

// A Wavetable card claims audio-file drops itself. Before issue #180 it returned false and the
// drop fell through to GraphEditor, which spawned an unrelated Sampler next to it.
TEST_F(ModuleComponentTest, WavetableCardAcceptsAudioFileDrag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    WavetableOscillatorModule processor;
    ModuleComponent moduleComponent(&processor, juce::AudioProcessorGraph::NodeID(1), editor);

    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag({"/tmp/table.wav"}));
    EXPECT_TRUE(moduleComponent.isInterestedInFileDrag({"/tmp/table.flac"}));
    EXPECT_FALSE(moduleComponent.isInterestedInFileDrag({"/tmp/notes.txt"}));
}
