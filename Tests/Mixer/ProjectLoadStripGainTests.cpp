// FRO149 repro/regression: a real .agsproj project (two Oscillator macros, each ending in a
// Channel Strip) opened with both strips' faders pinned to +12 dB (the top of the range) and the
// "12.0 dB" readout, regardless of what dB value was actually saved for that strip. This file
// pins the load path (ProjectBundle::load -> AIStateMapper::applyJSONToGraph, trusted) and the
// mixer UI path (MainComponent::openProjectForTest -> MixerPanelComponent::rebuild ->
// MixerColumnComponent::rebindControls -> MixerFader::bind) against a trimmed copy of that
// project's shape: two macros ("Oscillator 1" collapsed, "Oscillator 2" not), each an
// Oscillator feeding a Channel Strip, both strips feeding Master.
//
// Investigation (FRO149): the load path (ProjectBundle::load -> AIStateMapper::applyJSONToGraph,
// trusted), the real app's open-project flow (MainComponent::openProjectForTest ->
// GraphEditor::updateComponents -> reconcileTimelineAfterGraphChange), and the mixer UI path
// (MixerPanelComponent::rebuild -> MixerColumnComponent::rebindControls -> MixerFader::bind) were
// all checked -- including against the exact shape and connection/macro complexity of the real
// project file -- and every one of them faithfully preserves a strip's saved "gain" through to
// both the parameter and the fader readout. No corruption toward +12 dB was found anywhere in
// this path; these tests pin that each stage keeps a strip's saved gain intact as a regression
// guard, not as evidence of where a bug was fixed.

#include "AI/AIProvider.h"
#include "MainComponent/MainComponent.h"
#include "Modules/ChannelStripModule.h"
#include "ProjectBundle.h"
#include "UI/Mixer/MixerColumnComponent.h"
#include "UI/Mixer/MixerFader.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace {

// The strip's originally-saved gains (matches the FRO149 report): node "strip-a" was saved at
// essentially 0 dB (the same near-zero float noise the app's own dB<->linear round trip always
// produces for "unity"), node "strip-b" at -14.2 dB. Neither is anywhere near +12 dB.
constexpr float kStripAGainDb = -1.430511474609375e-6f;
constexpr float kStripBGainDb = -14.199997901916504f;
constexpr float kMasterGainDb = -1.430511474609375e-6f;

// A trimmed copy of the real project's shape (docs/mixer/mixer.md#node-types: two Oscillator macros, each
// terminating in a Channel Strip, both feeding Master). Node ids mirror the real file's
// numbering for easier cross-reference; everything not needed to reproduce the strip/gain/macro
// shape (ADSR, VCA, EQ, Compressor, sends, Delay, etc.) is left out.
juce::String makeProjectJson(float stripAGainDb, float stripBGainDb, float masterGainDb) {
    juce::String json = R"JSON(
{
  "schemaVersion": 1,
  "nodes": [
    { "id": 16, "type": "Audio Output", "uuid": "audio-output", "params": {} },
    { "id": 17, "type": "Oscillator", "uuid": "osc-a", "params": { "bypassed": false } },
    { "id": 23, "type": "Channel Strip", "uuid": "strip-a",
      "params": { "bypassed": false, "gain": )JSON";
    json << juce::String(stripAGainDb, 12)
         << R"JSON(, "pan": 0.0, "send1Level": 0.0, "send2Level": 0.0, "send3Level": 0.0,
                    "send4Level": 0.0, "muted": false },
      "state": { "shape": "stereo", "solo": false, "isBus": false, "sends": [] } },
    { "id": 24, "type": "Master", "uuid": "master",
      "params": { "bypassed": false, "gain": )JSON";
    json << juce::String(masterGainDb, 12) << R"JSON(, "muted": false } },
    { "id": 31, "type": "Oscillator", "uuid": "osc-b", "params": { "bypassed": false } },
    { "id": 37, "type": "Channel Strip", "uuid": "strip-b",
      "params": { "bypassed": false, "gain": )JSON";
    json << juce::String(stripBGainDb, 12)
         << R"JSON(, "pan": 0.0, "send1Level": 0.0, "send2Level": 0.0, "send3Level": 0.0,
                    "send4Level": 0.0, "muted": false },
      "state": { "shape": "stereo", "solo": false, "isBus": false, "sends": [] } }
  ],
  "connections": [
    { "src": 17, "srcPort": 0, "dst": 23, "dstPort": 0, "isMidi": false },
    { "src": 23, "srcPort": 0, "dst": 24, "dstPort": 0, "isMidi": false },
    { "src": 23, "srcPort": 4, "dst": 24, "dstPort": 1, "isMidi": false },
    { "src": 31, "srcPort": 0, "dst": 37, "dstPort": 0, "isMidi": false },
    { "src": 37, "srcPort": 0, "dst": 24, "dstPort": 0, "isMidi": false },
    { "src": 37, "srcPort": 4, "dst": 24, "dstPort": 1, "isMidi": false },
    { "src": 24, "srcPort": 0, "dst": 16, "dstPort": 0, "isMidi": false },
    { "src": 24, "srcPort": 1, "dst": 16, "dstPort": 1, "isMidi": false }
  ],
  "macros": [
    { "id": "macro-a", "name": "Oscillator 1", "colour": "ff5a7dff", "collapsed": true,
      "bounds": { "x": 0, "y": 0, "w": 280, "h": 90 },
      "members": [ "osc-a", "strip-a" ], "ports": [] },
    { "id": "macro-b", "name": "Oscillator 2", "colour": "ff5a7dff", "collapsed": false,
      "bounds": { "x": 0, "y": 800, "w": 280, "h": 90 },
      "members": [ "osc-b", "strip-b" ], "ports": [] }
  ]
}
)JSON";
    return json;
}

float getDenormalizedParam(juce::AudioProcessor& processor, const juce::String& paramId) {
    for (auto* p : processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(p))
            if (ranged->getParameterID() == paramId)
                return ranged->getNormalisableRange().convertFrom0to1(ranged->getValue());
    ADD_FAILURE() << "no parameter " << paramId;
    return 0.0f;
}

juce::AudioProcessor* findProcessorByUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->getProcessor();
    return nullptr;
}

juce::AudioProcessorGraph::NodeID findNodeIdByUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    for (auto* node : graph.getNodes())
        if (node->properties["uuid"].toString() == uuid)
            return node->nodeID;
    return {};
}

class MockProviderPLST : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPLST"; }
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback callback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        if (callback)
            callback(response);
        return {};
    }
    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { model = name; }
    juce::String getCurrentModel() const override { return model; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String model = "MockModel";
    int requestTimeoutMs = 240000;
};

} // namespace

class ProjectLoadStripGainTest : public ::testing::Test {
protected:
    void SetUp() override {
        root = juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-striploadgain-tests");
        root.deleteRecursively();
        root.createDirectory();
    }
    void TearDown() override { root.deleteRecursively(); }

    juce::File makeBundle(const juce::String& name, float stripAGainDb, float stripBGainDb, float masterGainDb) {
        auto dir = root.getChildFile(name + synth::ProjectBundle::kBundleExtension);
        dir.createDirectory();
        dir.getChildFile(synth::ProjectBundle::kAudioSubdirName).createDirectory();
        dir.getChildFile(synth::ProjectBundle::kPeaksSubdirName).createDirectory();
        dir.getChildFile(synth::ProjectBundle::kProjectFileName)
            .replaceWithText(makeProjectJson(stripAGainDb, stripBGainDb, masterGainDb));
        return dir;
    }

    juce::File root;
};

// Checkpoint A: ProjectBundle::load straight into a bare graph -- no UI, no canvas cards, no
// mixer panel. If this already shows +12 dB, the corruption is inside applyJSONToGraph/
// validatePatch/applyParamsToProcessor itself.
TEST_F(ProjectLoadStripGainTest, CheckpointA_BareProjectBundleLoadPreservesEachStripsSavedGain) {
    auto dir = makeBundle("CheckpointA", kStripAGainDb, kStripBGainDb, kMasterGainDb);

    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midiRemote;

    auto result = synth::ProjectBundle::load(dir, graph, timeline, patchDocument, macros, midiRemote);
    ASSERT_TRUE(result.ok) << result.message;

    auto* stripA = findProcessorByUuid(graph, "strip-a");
    auto* stripB = findProcessorByUuid(graph, "strip-b");
    ASSERT_NE(stripA, nullptr);
    ASSERT_NE(stripB, nullptr);

    EXPECT_NEAR(getDenormalizedParam(*stripA, "gain"), kStripAGainDb, 0.01f)
        << "strip-a's gain must round-trip through the trusted load, not clamp to +12 dB";
    EXPECT_NEAR(getDenormalizedParam(*stripB, "gain"), kStripBGainDb, 0.01f)
        << "strip-b's gain must round-trip through the trusted load, not clamp to +12 dB";
}

// Checkpoints B+C: the real app path -- MainComponent::openProjectForTest (the same
// openFromFile -> loadBundleFromFile flow the app's "Open Project" menu item drives), which
// additionally runs graphEditor.updateComponents() (canvas ModuleComponent cards, including the
// strips' own "Gain" knob, since Channel Strip is an ordinary card-visible node -- only excluded
// from the library and the AI, per docs/mixer/mixer.md#node-types) and reconcileTimelineAfterGraphChange().
// Then reveals the mixer panel and rebuilds it, exercising MixerColumnComponent::rebindControls
// -> MixerFader::bind. Checks the underlying parameter AND the fader's own slider value, so a
// regression that corrupts only the display (not the parameter) or only the parameter (not the
// display) is caught either way.
TEST_F(ProjectLoadStripGainTest, CheckpointBC_RealAppOpenAndMixerPanelPreserveEachStripsSavedGain) {
    auto dir = makeBundle("CheckpointBC", kStripAGainDb, kStripBGainDb, kMasterGainDb);

    MainComponent mc(std::make_unique<MockProviderPLST>());
    mc.setSize(1400, 900);
    ASSERT_TRUE(mc.openProjectForTest(dir));

    auto* stripA = findProcessorByUuid(mc.getAudioEngine().getGraph(), "strip-a");
    auto* stripB = findProcessorByUuid(mc.getAudioEngine().getGraph(), "strip-b");
    ASSERT_NE(stripA, nullptr);
    ASSERT_NE(stripB, nullptr);

    EXPECT_NEAR(getDenormalizedParam(*stripA, "gain"), kStripAGainDb, 0.01f)
        << "strip-a's gain after the real open-project flow (canvas cards rebuilt, timeline "
           "reconciled) must still be what was saved, not +12 dB";
    EXPECT_NEAR(getDenormalizedParam(*stripB, "gain"), kStripBGainDb, 0.01f)
        << "strip-b's gain after the real open-project flow must still be what was saved, not +12 dB";

    auto& mixerPanel = mc.getBottomDock().getMixerPanel();
    mixerPanel.rebuild();

    const auto stripANodeId = findNodeIdByUuid(mc.getAudioEngine().getGraph(), "strip-a");
    const auto stripBNodeId = findNodeIdByUuid(mc.getAudioEngine().getGraph(), "strip-b");
    ASSERT_TRUE(stripANodeId.uid != 0);
    ASSERT_TRUE(stripBNodeId.uid != 0);

    bool sawStripA = false, sawStripB = false;
    for (int i = 0; i < mixerPanel.getColumnCount(); ++i) {
        auto* column = mixerPanel.getStripColumnForTest(i);
        if (column == nullptr)
            continue;
        auto& fader = column->getFaderForTest();
        if (column->getNodeId() == stripANodeId) {
            sawStripA = true;
            ASSERT_TRUE(fader.isBoundForTest()) << "strip-a's column has no bound fader";
            EXPECT_NEAR(fader.getSlider().getValue(), (double)kStripAGainDb, 0.01)
                << "strip-a's mixer fader must read its saved dB, not +12 dB / the range max";
        } else if (column->getNodeId() == stripBNodeId) {
            sawStripB = true;
            ASSERT_TRUE(fader.isBoundForTest()) << "strip-b's column has no bound fader";
            EXPECT_NEAR(fader.getSlider().getValue(), (double)kStripBGainDb, 0.01)
                << "strip-b's mixer fader must read its saved dB, not +12 dB / the range max";
        }
    }
    EXPECT_TRUE(sawStripA) << "strip-a never showed up as a mixer column";
    EXPECT_TRUE(sawStripB) << "strip-b never showed up as a mixer column";
}

// Step 2: round-trip. Load the pre-corruption values, save, reload into a FRESH graph -- SAVING
// must not be what bakes a wrong value onto disk either.
TEST_F(ProjectLoadStripGainTest, LoadSaveReloadRoundTripsEachStripsGain) {
    auto dir = makeBundle("RoundTrip", kStripAGainDb, kStripBGainDb, kMasterGainDb);

    juce::AudioProcessorGraph graph;
    synth::TimelineDoc timeline;
    synth::PatchDocument patchDocument;
    synth::MacroSet macros;
    synth::MidiRemoteProjectDoc midiRemote;
    ASSERT_TRUE(synth::ProjectBundle::load(dir, graph, timeline, patchDocument, macros, midiRemote).ok);

    auto saveResult = synth::ProjectBundle::save(dir, graph, timeline, patchDocument, macros, midiRemote);
    ASSERT_TRUE(saveResult.ok) << saveResult.message;

    juce::AudioProcessorGraph freshGraph;
    synth::TimelineDoc freshTimeline;
    synth::PatchDocument freshPatchDocument;
    synth::MacroSet freshMacros;
    synth::MidiRemoteProjectDoc freshMidiRemote;
    auto reloadResult =
        synth::ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDocument, freshMacros, freshMidiRemote);
    ASSERT_TRUE(reloadResult.ok) << reloadResult.message;

    auto* stripA = findProcessorByUuid(freshGraph, "strip-a");
    auto* stripB = findProcessorByUuid(freshGraph, "strip-b");
    ASSERT_NE(stripA, nullptr);
    ASSERT_NE(stripB, nullptr);

    EXPECT_NEAR(getDenormalizedParam(*stripA, "gain"), kStripAGainDb, 0.01f)
        << "load -> save -> reload must keep strip-a's gain";
    EXPECT_NEAR(getDenormalizedParam(*stripB, "gain"), kStripBGainDb, 0.01f)
        << "load -> save -> reload must keep strip-b's gain (docs/mixer/mixer.md's own round-trip contract)";
}
