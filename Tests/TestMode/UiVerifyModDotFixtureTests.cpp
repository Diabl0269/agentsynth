// Tools/ui-verify/fixtures/mod-dot.agsproj: a Filter whose Cutoff is modulated by an LFO, for the mod dot scenarios.
// One test proves the committed fixture still loads with that routing; a disabled one regenerates it through the app's
// real save path:
//   build-ci-local/Tests/Tests --gtest_also_run_disabled_tests
//   --gtest_filter=UiVerifyModDotFixture.DISABLED_WritesModDotProject

#include "../Mixer/ChannelFlow/ChannelFlowTestFixture.h"
#include "AI/AIStateMapper/AIStateMapper.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "PatchDocument.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardBody/CardLayoutOverride.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {

juce::File fixtureBundle(const char* name = "mod-dot.agsproj") {
    return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile(name);
}

// A Filter whose Cutoff is modulated by `lfoCount` LFOs, saved through the app's real save path to `bundle`.
void writeFilterWithLfos(juce::File bundle, int lfoCount) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    auto& editor = mc.getGraphEditor();
    editor.addModuleAtCanvasPosition("Filter", {600, 200}, {});
    for (int i = 0; i < lfoCount; ++i)
        editor.addModuleAtCanvasPosition("LFO", {150, 100 + 220 * i}, {});
    auto& engine = mc.getAudioEngine();
    juce::AudioProcessorGraph::NodeID filterId;
    std::vector<juce::AudioProcessorGraph::NodeID> lfoIds;
    int cutoffChannel = -1;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->getProcessor()->getName().startsWith("LFO"))
            lfoIds.push_back(node->nodeID);
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()); module != nullptr) {
            for (const auto& target : module->getModulationTargets())
                if (node->getProcessor()->getName().startsWith("Filter") && target.paramId == "cutoff") {
                    filterId = node->nodeID;
                    cutoffChannel = target.channelIndex;
                }
        }
    }
    ASSERT_EQ(static_cast<int>(lfoIds.size()), lfoCount);
    ASSERT_NE(filterId.uid, 0u);
    float next = 0.5f;
    for (const auto lfoId : lfoIds) {
        const auto atten = engine.addModRouting(lfoId, 0, filterId, cutoffChannel);
        auto* node = engine.getGraph().getNodeForId(atten);
        ASSERT_NE(node, nullptr);
        auto* amount = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"));
        ASSERT_NE(amount, nullptr);
        amount->setValueNotifyingHost(amount->convertTo0to1(next));
        next = -0.25f;
    }

    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

// An Oscillator into a VCA whose Gain has no knob on the card (so its Gain CV jack is on the card) and two LFOs onto
// that jack (+50% and -25%), the first of them also on a Filter's Cutoff knob: the port connections panel lists
// audio, jack-landing and knob-landing modulations from the VCA's jacks and from an LFO's output.
void writePortPanelModProject(juce::File bundle) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    auto& editor = mc.getGraphEditor();
    editor.addModuleAtCanvasPosition("Oscillator", {80, 80}, {});
    editor.addModuleAtCanvasPosition("LFO", {80, 420}, {});
    editor.addModuleAtCanvasPosition("LFO", {80, 760}, {});
    editor.addModuleAtCanvasPosition("VCA", {1000, 420}, {});
    editor.addModuleAtCanvasPosition("Filter", {620, 420}, {});
    auto& engine = mc.getAudioEngine();
    juce::AudioProcessorGraph::NodeID oscId, vcaId, filterId;
    std::vector<juce::AudioProcessorGraph::NodeID> lfoIds;
    int gainChannel = -1, cutoffChannel = -1;
    for (auto* node : engine.getGraph().getNodes()) {
        const auto name = node->getProcessor()->getName();
        auto* module = dynamic_cast<ModuleBase*>(node->getProcessor());
        if (module == nullptr)
            continue;
        if (name.startsWith("LFO"))
            lfoIds.push_back(node->nodeID);
        if (name.startsWith("Oscillator"))
            oscId = node->nodeID;
        for (const auto& target : module->getModulationTargets()) {
            if (name.startsWith("VCA") && target.paramId == "gain") {
                vcaId = node->nodeID;
                gainChannel = target.channelIndex;
            }
            if (name.startsWith("Filter") && target.paramId == "cutoff") {
                filterId = node->nodeID;
                cutoffChannel = target.channelIndex;
            }
        }
    }
    ASSERT_EQ(static_cast<int>(lfoIds.size()), 2);
    ASSERT_NE(vcaId.uid, 0u);
    ASSERT_NE(filterId.uid, 0u);
    ASSERT_NE(oscId.uid, 0u);
    synth::CardSection section;
    section.id = "main";
    synth::CardLayout layout;
    layout.sections = {section};
    synth::setCardLayoutOverride(engine.getGraph(), nullptr, vcaId, layout); // no knobs: Gain keeps its CV jack
    auto setAmount = [&engine](juce::AudioProcessorGraph::NodeID atten, float value) {
        auto* node = engine.getGraph().getNodeForId(atten);
        ASSERT_NE(node, nullptr);
        auto* amount = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"));
        ASSERT_NE(amount, nullptr);
        amount->setValueNotifyingHost(amount->convertTo0to1(value));
    };
    editor.updateComponents();
    editor.connectPorts(oscId, 0, vcaId, 0, false, false);
    setAmount(engine.addModRouting(lfoIds[0], 0, vcaId, gainChannel), 0.5f);
    setAmount(engine.addModRouting(lfoIds[1], 0, vcaId, gainChannel), -0.25f);
    setAmount(engine.addModRouting(lfoIds[0], 0, filterId, cutoffChannel), 0.75f);

    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

int modulationCount(const juce::File& bundle) {
    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                   editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
    EXPECT_TRUE(result.ok) << result.message;
    const auto json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    const auto* modulations = json.getProperty("modulations", {}).getArray();
    return modulations != nullptr ? modulations->size() : -1;
}

} // namespace

class UiVerifyModDotFixture : public ChannelFlowTest {};

TEST_F(UiVerifyModDotFixture, DISABLED_WritesModDotProject) { writeFilterWithLfos(fixtureBundle(), 1); }

// Two LFOs on the Filter's Cutoff (+50% and -25%): the knob's hover chip lists both.
TEST_F(UiVerifyModDotFixture, DISABLED_WritesModDotTwoLfosProject) {
    writeFilterWithLfos(fixtureBundle("mod-dot-two-lfos.agsproj"), 2);
}

// Tools/ui-verify/fixtures/port-panel-mod.agsproj, for scenarios/port-panel-mod-controls.yaml.
TEST_F(UiVerifyModDotFixture, DISABLED_WritesPortPanelModProject) {
    writePortPanelModProject(fixtureBundle("port-panel-mod.agsproj"));
}

TEST_F(UiVerifyModDotFixture, TheCommittedPortPanelProjectHoldsThreeModulations) {
    const auto bundle = fixtureBundle("port-panel-mod.agsproj");
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();
    EXPECT_EQ(modulationCount(bundle), 3);
}

TEST_F(UiVerifyModDotFixture, TheCommittedModDotProjectHoldsAnLfoModulatingTheFiltersCutoff) {
    const auto bundle = fixtureBundle();
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();
    EXPECT_EQ(modulationCount(bundle), 1);
}

TEST_F(UiVerifyModDotFixture, TheCommittedTwoLfoProjectHoldsTwoLfosOnTheFiltersCutoff) {
    const auto bundle = fixtureBundle("mod-dot-two-lfos.agsproj");
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();
    EXPECT_EQ(modulationCount(bundle), 2);
}
