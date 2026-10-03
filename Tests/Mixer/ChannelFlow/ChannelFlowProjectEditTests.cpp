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

// Two tracks, each reusing its own envelope (7010 mono, 7020 poly) as the source of a modulation onto
// its Filter insert's cutoff (7011, 7021). Typical pluck times: they land exactly as written.
constexpr const char* kEnvelopePlanJson = R"({"mode": "merge", "nodes": [], "connections": [],
    "modulations": [{"source": 7010, "dest": 7011, "destParam": "cutoff", "amount": 0.5},
                    {"source": 7020, "dest": 7021, "destParam": "cutoff", "amount": 0.5}],
    "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator",
         "inserts": [{"type": "Filter", "id": 7011, "params": {"cutoff": 400}}],
         "envelope": {"id": 7010, "params": {"sustain": 0.0, "decay": 0.2, "release": 0.15}}},
        {"op": "addInstrumentTrack", "name": "Pad", "instrument": "Wavetable", "poly": true,
         "inserts": [{"type": "Filter", "id": 7021}],
         "envelope": {"id": 7020, "params": {"sustain": 0.25, "decay": 0.2}}}]})";

double rawParamPE(juce::AudioProcessor* processor, const juce::String& paramId) {
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param);
            ranged != nullptr && ranged->paramID == paramId)
            return ranged->convertFrom0to1(ranged->getValue());
    return -1.0;
}

bool cutoffIsModulatedBy(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::Node* source,
                         juce::AudioProcessorGraph::Node* filter) {
    const int cutoffChannel = dynamic_cast<ModuleBase*>(filter->getProcessor())->modulationChannelForParam("cutoff");
    for (auto* node : graph.getNodes())
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) != nullptr &&
            graph.isConnected({{source->nodeID, 0}, {node->nodeID, 0}}) &&
            graph.isConnected({{node->nodeID, 0}, {filter->nodeID, cutoffChannel}}))
            return true;
    return false;
}

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

// The envelope params land on the ADSR the build made, on the mono and on the poly path, and the
// envelope's id is a modulation source that reaches the Filter insert's cutoff through the real apply.
TEST_F(ChannelFlowTest, ProjectEditEnvelopeParamsLandOnTheBuiltAdsrAndItsIdModulatesTheInsertCutoff) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& graph = mc.getAudioEngine().getGraph();
    auto& aiService = mc.getAiServiceForTest();
    const auto before = snapshotCFT(mc);

    const juce::var plan = juce::JSON::parse(kEnvelopePlanJson);
    const auto preview = aiService.previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    const auto applied = aiService.applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;

    struct Expected {
        const char* track;
        double sustain;
        double decay;
        double release; // < 0: not set by the plan, left at the module default
    };
    for (const auto& expected : {Expected{"Bass", 0.0, 0.2, 0.15}, Expected{"Pad", 0.25, 0.2, -1.0}}) {
        SCOPED_TRACE(expected.track);
        const auto* macro = findMacroNamedPE(mc, expected.track);
        ASSERT_NE(macro, nullptr);
        auto* adsr = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::ADSR);
        auto* filter = findMacroMemberOfTypeCFT(graph, *macro, ModuleType::Filter);
        ASSERT_NE(adsr, nullptr);
        ASSERT_NE(filter, nullptr);
        EXPECT_NEAR(rawParamPE(adsr->getProcessor(), "sustain"), expected.sustain, 1.0e-3);
        EXPECT_NEAR(rawParamPE(adsr->getProcessor(), "decay"), expected.decay, 1.0e-3);
        if (expected.release >= 0.0)
            EXPECT_NEAR(rawParamPE(adsr->getProcessor(), "release"), expected.release, 1.0e-3);
        EXPECT_TRUE(cutoffIsModulatedBy(graph, adsr, filter)) << "envelope -> attenuverter -> Filter cutoff CV";
    }

    expectOneUndoStepCFT(mc, before);
}
