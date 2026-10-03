// ChannelFlowProjectEditTests.cpp
//
// One edit plan applied through the REAL app host (MainComponentTimelineOpsHost): the fixed apply
// order observable end to end. Track ops first (the insert the patch and the lane refer to exists
// before either needs it, though the lane is listed first), then the patch in merge mode with the
// plan's ids resolving to the built nodes, then the lane by nodeId - all as ONE undo step. The
// plan-wide rules are covered headless in
// Tests/AI/AIIntegrationService/AIIntegrationServiceProjectEditTests.cpp.

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "ChannelFlowTestFixture.h"
#include "MacroSet.h"
#include "MainComponent/MainComponent.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ModuleBase.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <set>

namespace {

// Insert Filter 7002 feeds patch Distortion 7004 (a connection FROM a built node); patch LFO 7003
// modulates the Filter's cutoff by name; the lane on the Filter is addressed by nodeId and listed
// before the op that builds it.
constexpr const char* kPlanJson = R"({"mode": "merge",
    "nodes": [{"id": 7003, "type": "LFO"}, {"id": 7004, "type": "Distortion"}],
    "connections": [{"src": 7002, "srcPort": 0, "dst": 7004, "dstPort": 0}],
    "modulations": [{"source": 7003, "dest": 7002, "destParam": "cutoff", "amount": 0.5}],
    "timelineOps": [
        {"op": "writeLane", "nodeId": 7002, "paramId": "cutoff",
         "points": [{"beat": 0, "value": 400, "curve": 1}, {"beat": 8, "value": 3000, "curve": 1}]},
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator", "instrumentId": 7001,
         "inserts": [{"type": "Filter", "id": 7002, "params": {"cutoff": 600}}]},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}]})";

const synth::Macro* findMacroNamedPE(MainComponent& mc, const juce::String& name) {
    for (const auto& macro : mc.getGraphEditor().getMacros().getAll())
        if (macro.name == name)
            return &macro;
    return nullptr;
}

juce::AudioProcessorGraph::Node* nodeOfFactoryType(juce::AudioProcessorGraph& graph, const juce::String& type,
                                                   const std::set<juce::AudioProcessorGraph::NodeID>& except) {
    for (auto* node : graph.getNodes())
        if (except.count(node->nodeID) == 0 && synth::AIStateMapper::getFactoryTypeName(node->getProcessor()) == type)
            return node;
    return nullptr;
}

} // namespace

TEST_F(ChannelFlowTest, ProjectEditAppliesTrackOpsThenPatchThenLanesAsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& aiService = mc.getAiServiceForTest();
    std::set<juce::AudioProcessorGraph::NodeID> liveBefore;
    for (auto* node : graph.getNodes()) {
        liveBefore.insert(node->nodeID);
        ASSERT_LT(node->nodeID.uid, 7001u) << "the plan's ids must not collide with a live uid";
    }
    const auto before = snapshotCFT(mc);

    const juce::var plan = juce::JSON::parse(kPlanJson);
    const auto preview = aiService.previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    expectSameSnapshotCFT(snapshotCFT(mc), before);

    const auto applied = aiService.applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;

    // Step 1 built the track and its Filter insert, inside the track's macro.
    const auto* macro = findMacroNamedPE(mc, "Bass");
    ASSERT_NE(macro, nullptr);
    auto* filter = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Filter);
    ASSERT_NE(filter, nullptr);
    const juce::String filterUuid = filter->properties["uuid"].toString();
    ASSERT_TRUE(filterUuid.isNotEmpty());

    // Step 2: the patch's nodes exist, the connection FROM the insert id landed on the built Filter,
    // and the destParam modulation feeds the Filter's cutoff CV channel through an attenuverter.
    auto* lfo = nodeOfFactoryType(graph, "LFO", liveBefore);
    auto* distortion = nodeOfFactoryType(graph, "Distortion", liveBefore);
    ASSERT_NE(lfo, nullptr);
    ASSERT_NE(distortion, nullptr);
    EXPECT_TRUE(graph.isConnected({{filter->nodeID, 0}, {distortion->nodeID, 0}}));
    const int cutoffChannel = dynamic_cast<ModuleBase*>(filter->getProcessor())->modulationChannelForParam("cutoff");
    ASSERT_GE(cutoffChannel, 0);
    bool modulated = false;
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr &&
            graph.isConnected({{lfo->nodeID, 0}, {node->nodeID, 0}}) &&
            graph.isConnected({{node->nodeID, 0}, {filter->nodeID, cutoffChannel}}))
            modulated = true;
    EXPECT_TRUE(modulated) << "LFO -> attenuverter -> Filter cutoff CV";

    // Step 3: the lane by nodeId is the Filter's, and the clip landed on the new track.
    const auto* lane = mc.getTimelineDoc().getLaneForParam(filterUuid, "cutoff");
    ASSERT_NE(lane, nullptr);
    EXPECT_EQ(lane->points.size(), 2u);
    const synth::Track* bass = nullptr;
    for (const auto& track : mc.getTimelineDoc().getTracks())
        bass = track.name == "Bass" ? &track : bass;
    ASSERT_NE(bass, nullptr);
    EXPECT_EQ(bass->clips.size(), 1u);

    // ONE undo step: undo restores graph, doc and macros exactly; redo rebuilds all of it.
    expectOneUndoStepCFT(mc, before);
    ASSERT_TRUE(mc.getUndoManager().undo());
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_EQ(findMacroNamedPE(mc, "Bass"), nullptr);
}
