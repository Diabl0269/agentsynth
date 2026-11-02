// Tools/ui-verify/fixtures/lane-rows.agsproj: a MIDI track playing a Filter (Track In -> Oscillator -> Filter) with a
// Cutoff automation lane carrying two LFO modulator rows, for the per-row resize scenario. One test proves the
// committed fixture still loads with that lane and both routings; a disabled one regenerates it through the app's real
// save path:
//   build-ci-local/Tests/Tests --gtest_also_run_disabled_tests
//   --gtest_filter=UiVerifyLaneRowsFixture.DISABLED_WritesLaneRowsProject

#include "../UI/Timeline/AutomationLanes/AutomationLanesModulatorFixture.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "ProjectBundle.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>

namespace {

juce::File fixtureBundle() {
    return juce::File(juce::String(UI_VERIFY_FIXTURES_DIR)).getChildFile("lane-rows.agsproj");
}

} // namespace

TEST(UiVerifyLaneRowsFixture, DISABLED_WritesLaneRowsProject) {
    modulator_test::Scene scene;
    auto osc = modulator_test::addNodeWithUuid(scene.mc, "Oscillator");
    ASSERT_NE(osc, nullptr);
    for (auto* node : scene.graph().getNodes())
        if (node->properties["uuid"].toString() == scene.doc().getTracks().front().bindingUuid)
            ASSERT_TRUE(scene.graph().addConnection({{node->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
                                                     {osc->nodeID, juce::AudioProcessorGraph::midiChannelIndex}}));
    ASSERT_TRUE(scene.graph().addConnection({{osc->nodeID, 0}, {scene.target->nodeID, 0}}));
    scene.mc.getGraphEditor().updateComponents();
    scene.addLfoFromLaneMenu();
    scene.addLfoFromLaneMenu();
    ASSERT_NE(scene.row(1), nullptr) << "two modulator rows on the Cutoff lane";

    const auto bundle = fixtureBundle();
    bundle.deleteRecursively();
    bundle.getParentDirectory().createDirectory();
    ASSERT_TRUE(scene.mc.saveProjectForTest(bundle));
    ASSERT_TRUE(synth::ProjectBundle::isBundle(bundle));
}

TEST(UiVerifyLaneRowsFixture, TheCommittedProjectHoldsACutoffLaneWithTwoModulators) {
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
    ASSERT_EQ(timeline.getTracks().size(), 1u);
    int cutoffLanes = 0;
    for (const auto& lane : timeline.getTracks().front().lanes)
        cutoffLanes += lane.paramId == "cutoff" ? 1 : 0;
    EXPECT_EQ(cutoffLanes, 1);
    const auto json = synth::AIStateMapper::graphToJSON(engine.getGraph());
    const auto* modulations = json.getProperty("modulations", {}).getArray();
    ASSERT_NE(modulations, nullptr);
    std::map<int, juce::String> types; // the starter patch has envelope routings of its own
    if (const auto* nodes = json.getProperty("nodes", {}).getArray())
        for (const auto& n : *nodes)
            types[(int)n.getProperty("id", -1)] = n.getProperty("type", {}).toString();
    int fromLfos = 0;
    for (const auto& m : *modulations)
        fromLfos += types[(int)m.getProperty("source", -1)] == "LFO" ? 1 : 0;
    EXPECT_EQ(fromLfos, 2);
}
