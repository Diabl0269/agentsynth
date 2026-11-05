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
