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
#include "Music/SongKey.h"
#include "TestPaths.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <gtest/gtest.h>
#include <set>

namespace {

// Streams one scripted final line for /v1/song/build; every other call is inert.
class SongBuildProvider : public synth::AIProvider {
public:
    explicit SongBuildProvider(juce::String line)
        : doneLine(std::move(line)) {}
    RequestId sendPrompt(const std::vector<Message>&, CompletionCallback, const juce::var&,
                         std::function<void(const juce::String&)> = {}) override {
        return {};
    }
    RequestId streamRouteRequest(const juce::String&, const juce::var&, StreamLineCallback onLine,
                                 CompletionCallback onFinished) override {
        onLine(doneLine);
        AIResponse response;
        response.success = true;
        onFinished(response);
        return {2};
    }
    void cancel(RequestId) override {}
    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> cb) override { cb({}, true); }
    void setModel(const juce::String&) override {}
    juce::String getCurrentModel() const override { return {}; }
    void setRequestTimeoutMs(int) override {}
    int getRequestTimeoutMs() const override { return 1000; }
    juce::String getProviderName() const override { return "SongBuildProvider"; }
    bool isHosted() const override { return true; }

private:
    juce::String doneLine;
};

// Builds a song whose plan names `planKey`, answering with a final envelope that adds one track.
synth::AIIntegrationService::SongBuildOutcome buildSongWithKey(MainComponent& mc, const juce::String& planKey) {
    mc.getAiServiceForTest().setProvider(std::make_unique<SongBuildProvider>(
        R"({"type":"done","notes":[],"envelope":{"mode":"merge","nodes":[],"connections":[],"timelineOps":[
            {"op":"addInstrumentTrack","name":"Bass","instrument":"Oscillator"}]}})"));
    synth::songsteps::PlanReply reply;
    juce::String error;
    const juce::String planJson = R"({"plan":{"title":"T","bpm":120,"key":")" + planKey +
                                  R"(","special":"",
        "sections":[{"name":"Intro","bars":16,"energy":1,"chords":"Am","description":"pad"}],
        "tracks":[{"name":"Kit","role":"drums","instrument":"Drum Kit","sound":"punchy","sections":["Intro"]}],
        "lanes":[]},"price":{"units":1,"estimatedUsd":0.01},"steps":[],"token":"t"})";
    EXPECT_TRUE(synth::songsteps::parsePlanReply(planJson, reply, error)) << error;
    synth::AIIntegrationService::SongBuildOutcome outcome;
    synth::AIIntegrationService::SongBuildCallbacks callbacks;
    callbacks.onFinished = [&](const auto& o) { outcome = o; };
    mc.getAiServiceForTest().buildSong("a song", reply, callbacks);
    return outcome;
}

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

// A song's tempo and section markers through the real host: the tempo reaches the transport, the
// markers the doc, and ONE undo restores the tempo, the markers and the track together. The
// transport poll must not then record the tempo a second time as its own step.
TEST_F(ChannelFlowTest, ProjectEditTempoAndMarkersAreOneUndoStepWithTheRest) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    auto& aiService = mc.getAiServiceForTest();
    const auto before = snapshotCFT(mc);
    const double bpmBefore = transport.getDocumentState().bpm;
    ASSERT_NE(bpmBefore, 174.0);

    const juce::var plan = juce::JSON::parse(R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
        {"op": "setTempo", "bpm": 174},
        {"op": "addMarker", "beat": 0, "name": "Intro"}, {"op": "addMarker", "beat": 16, "name": "Drop"},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 36, "velocity": 100}]}]}]})");
    const auto preview = aiService.previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_EQ(transport.getDocumentState().bpm, bpmBefore) << "a preview never moves the tempo";
    expectSameSnapshotCFT(snapshotCFT(mc), before);

    const auto applied = aiService.applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(transport.getDocumentState().bpm, 174.0);
    ASSERT_EQ(mc.getTimelineDoc().getMarkers().size(), 2u);
    EXPECT_EQ(mc.getTimelineDoc().getMarkers()[1].text, "Drop");

    // The debounced poll finds the transport already recorded, so it adds no second step.
    mc.pollTransportEditsForTest(1000);
    mc.pollTransportEditsForTest(5000);

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(transport.getDocumentState().bpm, bpmBefore) << "undo restores the tempo";
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_TRUE(mc.getTimelineDoc().getMarkers().empty());

    ASSERT_TRUE(mc.getUndoManager().redo());
    EXPECT_EQ(transport.getDocumentState().bpm, 174.0) << "redo sets it again";
    EXPECT_EQ(mc.getTimelineDoc().getMarkers().size(), 2u);
}

// A tempo-only plan (nothing graph-side) still lands as one undo step.
TEST_F(ChannelFlowTest, ProjectEditTempoAloneIsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    const double bpmBefore = transport.getDocumentState().bpm;

    const auto applied = mc.getAiServiceForTest().applyProjectEdit(juce::JSON::parse(
        R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [{"op": "setTempo", "bpm": 90}]})"));
    ASSERT_TRUE(applied.ok) << applied.message;
    EXPECT_EQ(transport.getDocumentState().bpm, 90.0);
    EXPECT_NE(applied.message, "The project already matched this edit plan, so nothing changed.");

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(transport.getDocumentState().bpm, bpmBefore);
}

// The AI's song key through the real host: the key reaches the transport, ONE undo restores the previous key (No
// key) and the track together, and the transport poll records no extra step.
TEST_F(ChannelFlowTest, ProjectEditSongKeyIsOneUndoStepWithTheRest) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    auto& aiService = mc.getAiServiceForTest();
    const auto before = snapshotCFT(mc);
    ASSERT_FALSE(transport.getDocumentState().songKey.has_value());

    const juce::var plan = juce::JSON::parse(R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [
        {"op": "addInstrumentTrack", "name": "Bass", "instrument": "Oscillator"},
        {"op": "setSongKey", "root": "D", "scale": "Dorian"},
        {"op": "placeClips", "track": "Bass", "clips": [{"startBeat": 0, "lengthBeats": 4, "notes": [
            {"startBeat": 0, "lengthBeats": 1, "pitch": 38, "velocity": 100}]}]}]})");
    const auto preview = aiService.previewProjectEdit(plan);
    ASSERT_TRUE(preview.ok) << preview.message;
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value()) << "a preview never sets the key";

    const auto applied = aiService.applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(transport.getDocumentState().songKey.has_value());
    EXPECT_EQ(synth::keyDisplayName(*transport.getDocumentState().songKey), "D Dorian");

    mc.pollTransportEditsForTest(1000);
    mc.pollTransportEditsForTest(5000);

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value()) << "undo restores No key";
    expectSameSnapshotCFT(snapshotCFT(mc), before);
    EXPECT_FALSE(mc.getUndoManager().canUndo()) << "the poll recorded no extra step";

    ASSERT_TRUE(mc.getUndoManager().redo());
    ASSERT_TRUE(transport.getDocumentState().songKey.has_value());
    EXPECT_EQ(synth::keyDisplayName(*transport.getDocumentState().songKey), "D Dorian") << "redo sets it again";
}

// A song build sets the Key field from the plan: ONE undo restores the previous key and the new track together.
TEST_F(ChannelFlowTest, SongBuildSetsThePlanKeyAsPartOfItsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    transport.setSongKey(synth::makeScale(0, synth::scalePresetIndex("Major")));
    mc.pollTransportEditsForTest(1000);
    mc.pollTransportEditsForTest(5000);
    const auto keyBefore = transport.getDocumentState().songKey;
    const auto undoneToBefore = snapshotCFT(mc);

    const auto outcome = buildSongWithKey(mc, "F# minor");
    ASSERT_EQ(outcome.kind, synth::AIIntegrationService::SongBuildOutcome::Kind::applied) << outcome.message;
    ASSERT_TRUE(transport.getDocumentState().songKey.has_value());
    EXPECT_EQ(synth::keyDisplayName(*transport.getDocumentState().songKey), "F# Natural Minor");
    mc.pollTransportEditsForTest(1000);
    mc.pollTransportEditsForTest(5000);

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_TRUE(synth::sameKey(transport.getDocumentState().songKey, keyBefore)) << "undo restores the previous key";
    expectSameSnapshotCFT(snapshotCFT(mc), undoneToBefore);
}

// A key the plan does not state readably leaves the project's key untouched.
TEST_F(ChannelFlowTest, SongBuildWithAnUnreadablePlanKeyLeavesTheKeyAlone) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();
    transport.setSongKey(synth::makeScale(2, synth::scalePresetIndex("Dorian")));
    const auto keyBefore = transport.getDocumentState().songKey;

    const auto outcome = buildSongWithKey(mc, "no idea");
    ASSERT_EQ(outcome.kind, synth::AIIntegrationService::SongBuildOutcome::Kind::applied) << outcome.message;
    EXPECT_TRUE(synth::sameKey(transport.getDocumentState().songKey, keyBefore));
}

// A key-only plan still lands as one undo step.
TEST_F(ChannelFlowTest, ProjectEditSongKeyAloneIsOneUndoStep) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    auto& transport = mc.getAudioEngine().getTransport();

    const auto applied = mc.getAiServiceForTest().applyProjectEdit(juce::JSON::parse(
        R"({"mode": "merge", "nodes": [], "connections": [], "timelineOps": [{"op": "setSongKey", "root": "Bb", "scale": "Major"}]})"));
    ASSERT_TRUE(applied.ok) << applied.message;
    ASSERT_TRUE(transport.getDocumentState().songKey.has_value());
    EXPECT_EQ(synth::keyDisplayName(*transport.getDocumentState().songKey), "A# Major");

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_FALSE(transport.getDocumentState().songKey.has_value());
}

// Regression test: a real full-song answer (Tests/fixtures/ai-plans/dark-forest-plan.json, its ids offset by 7000
// clear of a fresh project's live uids) writes six
// lanes on its tracks' inserts, envelopes and instrument. Each must land on the track that plays its
// module and read as a named parameter, not pile up in the Unassigned section of the shared Automation track.
TEST_F(ChannelFlowTest, ProjectEditFullSongLanesLandOnTheirOwningTracks) {
    MainComponent mc(std::make_unique<MockProviderCFT>());
    mc.setSize(1600, 900);
    mc.getAudioEngine().suspendDeviceCallback();
    const auto fixture = juce::File(TESTS_ROOT_DIR).getChildFile("fixtures/ai-plans/dark-forest-plan.json");
    const juce::var plan = juce::JSON::parse(fixture.loadFileAsString());
    ASSERT_FALSE(plan.isVoid()) << fixture.getFullPathName();

    const auto tracksBefore = mc.getTimelineDoc().getTracks().size();
    const auto applied = mc.getAiServiceForTest().applyProjectEdit(plan);
    ASSERT_TRUE(applied.ok) << applied.message;

    const auto& doc = mc.getTimelineDoc();
    const auto& graph = mc.getAudioEngine().getGraph();
    const auto hasNode = [&](const juce::String& uuid) {
        for (auto* node : graph.getNodes())
            if (node->properties["uuid"].toString() == uuid)
                return true;
        return false;
    };
    int lanes = 0;
    for (const auto& track : doc.getTracks()) {
        EXPECT_NE(track.kind, synth::TrackKind::Automation)
            << "no lane may be left on the shared Automation track (" << track.lanes.size() << " there)";
        for (const auto& lane : track.lanes) {
            ++lanes;
            SCOPED_TRACE(track.name + " / " + lane.paramId);
            EXPECT_TRUE(hasNode(lane.nodeUuid)) << "the lane names a real node";
            EXPECT_FALSE(lane.orphaned);
        }
    }
    EXPECT_EQ(lanes, 6);

    const auto laneOn = [&](const char* trackName, const char* paramId) {
        for (const auto& track : doc.getTracks())
            if (track.name == trackName)
                for (const auto& lane : track.lanes)
                    if (lane.paramId == paramId)
                        return true;
        return false;
    };
    EXPECT_TRUE(laneOn("Forest Bass", "cutoff"));
    EXPECT_TRUE(laneOn("Forest Bass", "resonance"));
    EXPECT_TRUE(laneOn("Forest Lead", "cutoff"));
    EXPECT_TRUE(laneOn("Forest Lead", "resonance"));
    EXPECT_TRUE(laneOn("Forest Lead", "release"));
    EXPECT_TRUE(laneOn("Forest Pad", "level"));

    ASSERT_TRUE(mc.getUndoManager().undo());
    EXPECT_EQ(mc.getTimelineDoc().getTracks().size(), tracksBefore) << "one undo removes the tracks and every lane";
}
