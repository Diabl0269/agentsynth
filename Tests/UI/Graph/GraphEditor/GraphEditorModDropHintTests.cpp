// FRO289: the one-time "Drop it on any knob to modulate that parameter" status-bar hint, fired
// from GraphEditor::beginConnectionDrag the first time a drag starts from a modulation source's
// OUTPUT -- never again once shown, never for an audio/MIDI output, never for an INPUT drag.

#include "GraphEditorTestHelpers.h"
#include "Modules/LFOModule.h"
#include "Modules/OscillatorModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

// A throwaway temp file, never the real user settings (mirrors PianoRollTestHelpers.h's
// makeScaleAssistTestProps -- setPropertiesFile takes a real juce::PropertiesFile*, not the
// ApplicationProperties wrapper).
std::unique_ptr<juce::PropertiesFile> makeTestProps(const juce::String& name) {
    auto file = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile(name + ".settings");
    file.deleteFile();
    juce::PropertiesFile::Options opts;
    opts.applicationName = name;
    opts.filenameSuffix = "settings";
    return std::make_unique<juce::PropertiesFile>(file, opts);
}

} // namespace

TEST_F(GraphEditorTest, FirstModSourceDragShowsTheHintAndSetsTheFlag) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto props = makeTestProps("GraphEditorModDropHintTest_First");
    editor.setPropertiesFile(props.get());
    ASSERT_FALSE(props->getBoolValue(synth::kModDropHintShownSettingKey, false));

    LFOModule lfo;
    ModuleComponent lfoComp(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };

    editor.beginConnectionDrag(&lfoComp, 0, /*isInput*/ false, /*isMidi*/ false, {0, 0});

    EXPECT_EQ(lastStatus, "Drop it on any knob to modulate that parameter");
    EXPECT_TRUE(props->getBoolValue(synth::kModDropHintShownSettingKey, false));
}

TEST_F(GraphEditorTest, SecondModSourceDragDoesNotShowTheHintAgain) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto props = makeTestProps("GraphEditorModDropHintTest_Second");
    editor.setPropertiesFile(props.get());

    LFOModule lfo;
    ModuleComponent lfoComp(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);

    editor.beginConnectionDrag(&lfoComp, 0, false, false, {0, 0}); // first drag: shows + sets flag

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };
    editor.beginConnectionDrag(&lfoComp, 0, false, false, {0, 0}); // second: must stay silent

    EXPECT_TRUE(lastStatus.isEmpty());
}

TEST_F(GraphEditorTest, AudioOutputDragNeverShowsTheHint) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto props = makeTestProps("GraphEditorModDropHintTest_AudioOut");
    editor.setPropertiesFile(props.get());

    OscillatorModule osc;
    ModuleComponent oscComp(&osc, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };
    // Oscillator's ch0 output is its audio signal, not a ModCV jack.
    editor.beginConnectionDrag(&oscComp, 0, false, false, {0, 0});

    EXPECT_TRUE(lastStatus.isEmpty());
    EXPECT_FALSE(props->getBoolValue(synth::kModDropHintShownSettingKey, false));
}

TEST_F(GraphEditorTest, InputDragNeverShowsTheHintEvenForAModSource) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto props = makeTestProps("GraphEditorModDropHintTest_Input");
    editor.setPropertiesFile(props.get());

    LFOModule lfo;
    ModuleComponent lfoComp(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };
    editor.beginConnectionDrag(&lfoComp, 0, /*isInput*/ true, false, {0, 0});

    EXPECT_TRUE(lastStatus.isEmpty());
}

TEST_F(GraphEditorTest, ModSourceDragAlreadyFlaggedShownStaysSilent) {
    AudioEngine engine;
    GraphEditor editor(engine);
    auto props = makeTestProps("GraphEditorModDropHintTest_PreFlagged");
    props->setValue(synth::kModDropHintShownSettingKey, true);
    editor.setPropertiesFile(props.get());

    LFOModule lfo;
    ModuleComponent lfoComp(&lfo, juce::AudioProcessorGraph::NodeID(1), editor);

    juce::String lastStatus;
    editor.onStatusMessage = [&lastStatus](const juce::String& msg) { lastStatus = msg; };
    editor.beginConnectionDrag(&lfoComp, 0, false, false, {0, 0});

    EXPECT_TRUE(lastStatus.isEmpty());
}
