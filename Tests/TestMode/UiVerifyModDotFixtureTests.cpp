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

juce::File fixtureBundle() { return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("mod-dot.agsproj"); }

} // namespace

class UiVerifyModDotFixture : public ChannelFlowTest {};

TEST_F(UiVerifyModDotFixture, DISABLED_WritesModDotProject) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    mc.newPatchForTest();

    auto& editor = mc.getGraphEditor();
    editor.addModuleAtCanvasPosition("Filter", {600, 200}, {});
    editor.addModuleAtCanvasPosition("LFO", {150, 200}, {});
    auto& engine = mc.getAudioEngine();
    juce::AudioProcessorGraph::NodeID filterId, lfoId;
    int cutoffChannel = -1;
    for (auto* node : engine.getGraph().getNodes()) {
        if (node->getProcessor()->getName().startsWith("LFO"))
            lfoId = node->nodeID;
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()); module != nullptr) {
            for (const auto& target : module->getModulationTargets())
                if (node->getProcessor()->getName().startsWith("Filter") && target.paramId == "cutoff") {
                    filterId = node->nodeID;
                    cutoffChannel = target.channelIndex;
                }
        }
    }
    ASSERT_NE(lfoId.uid, 0u);
    ASSERT_NE(filterId.uid, 0u);
    const auto atten = engine.addModRouting(lfoId, 0, filterId, cutoffChannel);
    auto* node = engine.getGraph().getNodeForId(atten);
    ASSERT_NE(node, nullptr);
    auto* amount = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount"));
    ASSERT_NE(amount, nullptr);
    amount->setValueNotifyingHost(amount->convertTo0to1(0.5f));

    const auto bundle = fixtureBundle();
    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(mc.saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

TEST_F(UiVerifyModDotFixture, TheCommittedModDotProjectHoldsAnLfoModulatingTheFiltersCutoff) {
    const auto bundle = fixtureBundle();
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle)) << bundle.getFullPathName();

    AudioEngine engine;
    AppUndoManager undo;
    GraphEditor editor{engine, &undo};
    synth::TimelineDoc timeline;
    synth::MidiRemoteProjectDoc midiRemote;
    const auto result = synth::ProjectBundle::load(bundle, engine.getGraph(), timeline, editor.getPatchDocument(),
                                                   editor.getMacros(), midiRemote, nullptr, nullptr, nullptr, nullptr);
    ASSERT_TRUE(result.ok) << result.message;
    const auto json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    const auto* modulations = json.getProperty("modulations", {}).getArray();
    ASSERT_NE(modulations, nullptr);
    EXPECT_EQ(modulations->size(), 1);
}
