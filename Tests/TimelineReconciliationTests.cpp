#include "../Source/AI/AIStateMapper.h"
#include "../Source/AppUndoManager.h"
#include "../Source/Modules/FilterModule.h"
#include "../Source/Modules/OscillatorModule.h"
#include "../Source/Modules/VCAModule.h"
#include "../Source/PatchDocument.h"
#include "../Source/ProjectBundle.h"
#include "../Source/Timeline/TimelineDoc.h"
#include "../Source/Timeline/TimelineReconciler.h"
#include <gtest/gtest.h>
#include <initializer_list>
#include <juce_audio_processors/juce_audio_processors.h>
#include <tuple>

// Lane/track binding reconciliation. The rule under test throughout: an unresolvable
// uuid-keyed binding (Track::bindingUuid, AutomationLane::nodeUuid) becomes ORPHANED — retained,
// flagged, re-bindable — and is never auto-deleted. See docs/architecture.md's TimelineDoc /
// ProjectBundle sections for the full policy and the AI-merge-renumbering rationale.

using synth::AutomationLane;
using synth::LaneId;
using synth::PatchDocument;
using synth::ProjectBundle;
using synth::TimelineDoc;
using synth::TimelineReconciler;
using synth::TrackId;
using synth::TrackKind;

namespace {

class CountingListener : public TimelineDoc::Listener {
public:
    void timelineChanged(const TimelineDoc&) override { ++calls; }
    int calls = 0;
};

AutomationLane::RangeSnapshot makeRange(float minValue = 0.0f, float maxValue = 1.0f, float defaultValue = 0.5f) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

// A minimal, hand-built trusted patch: just enough for validatePatch's trusted (structural-only)
// path — "nodes" + "connections" arrays. Each node carries an explicit "id" and "uuid", adopted
// verbatim because the caller applies it with trusted=true.
juce::var makeTrustedPatch(std::initializer_list<std::tuple<int, juce::String, juce::String>> nodes) {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    juce::Array<juce::var> nodeArr;
    for (const auto& [id, type, uuid] : nodes) {
        juce::DynamicObject::Ptr n = new juce::DynamicObject();
        n->setProperty("id", id);
        n->setProperty("type", type);
        n->setProperty("uuid", uuid);
        nodeArr.add(juce::var(n.get()));
    }
    root->setProperty("nodes", juce::var(nodeArr));
    root->setProperty("connections", juce::var(juce::Array<juce::var>()));
    return juce::var(root.get());
}

} // namespace

class TimelineReconciliationTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    TimelineDoc doc;
    CountingListener listener;

    void SetUp() override {
        graph.clear();
        doc.addListener(&listener);
    }
    void TearDown() override { doc.removeListener(&listener); }
};

// =============================================================================
// 1. UnresolvedLaneIsOrphanedNeverDeleted
// =============================================================================

TEST_F(TimelineReconciliationTest, UnresolvedLaneIsOrphanedNeverDeleted) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    doc.setTrackBinding(track, "missing-track-target");
    const auto laneA = doc.addLane(track, "missing-lane-target-a", "cutoff", makeRange());
    const auto laneB = doc.addLane(track, "missing-lane-target-b", "resonance", makeRange());
    ASSERT_TRUE(laneA.isValid());
    ASSERT_TRUE(laneB.isValid());

    const auto revisionBefore = doc.getRevision();
    const int callsBefore = listener.calls;

    // graph is empty: none of the three bindings above resolve to anything.
    EXPECT_TRUE(TimelineReconciler::reconcile(doc, graph));

    EXPECT_EQ(doc.getRevision(), revisionBefore + 1) << "several broken bindings must still be ONE revision bump";
    EXPECT_EQ(listener.calls, callsBefore + 1) << "and exactly one listener notification";

    // Never deleted.
    ASSERT_NE(doc.getTrack(track), nullptr);
    ASSERT_NE(doc.getLane(laneA), nullptr);
    ASSERT_NE(doc.getLane(laneB), nullptr);
    EXPECT_TRUE(doc.getTrack(track)->orphaned);
    EXPECT_TRUE(doc.getLane(laneA)->orphaned);
    EXPECT_TRUE(doc.getLane(laneB)->orphaned);
}

// =============================================================================
// 2. EmptyBindingIsUnboundNotOrphaned
// =============================================================================

TEST_F(TimelineReconciliationTest, EmptyBindingIsUnboundNotOrphaned) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead"); // bindingUuid left empty
    ASSERT_TRUE(doc.getTrack(track)->bindingUuid.isEmpty());

    // Reconciling against an empty graph must not flag an unbound track.
    EXPECT_FALSE(TimelineReconciler::reconcile(doc, graph)) << "nothing to flip: a no-op reconcile";
    EXPECT_FALSE(doc.getTrack(track)->orphaned);
}

// =============================================================================
// 3. ResolutionClearsOrphan
// =============================================================================

TEST_F(TimelineReconciliationTest, ResolutionClearsOrphan) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = doc.addLane(track, "node-uuid", "cutoff", makeRange());
    ASSERT_TRUE(lane.isValid());

    ASSERT_TRUE(TimelineReconciler::reconcile(doc, graph)); // empty graph: orphans it
    ASSERT_TRUE(doc.getLane(lane)->orphaned);

    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    node->properties.set("uuid", "node-uuid");

    EXPECT_TRUE(TimelineReconciler::reconcile(doc, graph));
    EXPECT_FALSE(doc.getLane(lane)->orphaned);
}

// =============================================================================
// 4. ReconcileTwiceIsNoOp
// =============================================================================

TEST_F(TimelineReconciliationTest, ReconcileTwiceIsNoOp) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = doc.addLane(track, "missing-uuid", "cutoff", makeRange());
    ASSERT_TRUE(lane.isValid());

    ASSERT_TRUE(TimelineReconciler::reconcile(doc, graph)); // first pass: orphans it
    const auto revisionAfterFirst = doc.getRevision();
    const int callsAfterFirst = listener.calls;

    EXPECT_FALSE(TimelineReconciler::reconcile(doc, graph)) << "same graph, same flags: a no-op";
    EXPECT_EQ(doc.getRevision(), revisionAfterFirst);
    EXPECT_EQ(listener.calls, callsAfterFirst);
}

// =============================================================================
// 5. MergeRenumberDoesNotOrphan (THE motivating scenario, pinned against real applyJSONToGraph)
// =============================================================================

TEST_F(TimelineReconciliationTest, MergeRenumberDoesNotOrphan) {
    // Trusted full apply with EXPLICIT node ids and uuids — the graph a save/load or an undo
    // restore would produce.
    auto initialPatch = makeTrustedPatch({{1, "Oscillator", "osc-uuid"}, {2, "Filter", "filter-uuid"}});
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(initialPatch, graph, /*clearExisting=*/true, /*trusted=*/true));

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    doc.setTrackBinding(track, "osc-uuid");
    const auto lane = doc.addLane(track, "filter-uuid", "cutoff", makeRange());
    ASSERT_TRUE(lane.isValid());

    ASSERT_FALSE(TimelineReconciler::reconcile(doc, graph)) << "both bindings already resolve: no-op";
    ASSERT_FALSE(doc.getTrack(track)->orphaned);
    ASSERT_FALSE(doc.getLane(lane)->orphaned);

    // Merge-mode trusted apply that ADDS a node under an id ("99") that names nothing in the live
    // graph — a concrete instance of AI merge patches renumbering ids routinely: the patch's own
    // id numbering does not correspond to real graph ids, only uuids survive the round trip.
    auto mergePatch = makeTrustedPatch({{99, "VCA", "vca-uuid"}});
    ASSERT_TRUE(synth::AIStateMapper::applyJSONToGraph(mergePatch, graph, /*clearExisting=*/false, /*trusted=*/true));
    ASSERT_EQ(graph.getNumNodes(), 3);

    EXPECT_FALSE(TimelineReconciler::reconcile(doc, graph)) << "the merge must not have orphaned anything";
    EXPECT_FALSE(doc.getTrack(track)->orphaned);
    EXPECT_FALSE(doc.getLane(lane)->orphaned);
}

// =============================================================================
// 6. NodeDeletionOrphansItsBindings
// =============================================================================

TEST_F(TimelineReconciliationTest, NodeDeletionOrphansItsBindings) {
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "osc-uuid");

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    doc.setTrackBinding(track, "osc-uuid");
    const auto lane = doc.addLane(track, "osc-uuid", "freq", makeRange());
    ASSERT_TRUE(lane.isValid());

    ASSERT_FALSE(TimelineReconciler::reconcile(doc, graph)); // resolves fine beforehand
    ASSERT_FALSE(doc.getTrack(track)->orphaned);
    ASSERT_FALSE(doc.getLane(lane)->orphaned);

    graph.removeNode(osc->nodeID);

    EXPECT_TRUE(TimelineReconciler::reconcile(doc, graph));
    ASSERT_NE(doc.getTrack(track), nullptr) << "never deleted";
    ASSERT_NE(doc.getLane(lane), nullptr) << "never deleted";
    EXPECT_TRUE(doc.getTrack(track)->orphaned);
    EXPECT_TRUE(doc.getLane(lane)->orphaned);
}

// =============================================================================
// 7. UndoRestoreClearsOrphanAfterReconcile
// =============================================================================

TEST_F(TimelineReconciliationTest, UndoRestoreClearsOrphanAfterReconcile) {
    auto filter = graph.addNode(std::make_unique<FilterModule>());
    filter->properties.set("uuid", "filter-uuid");
    const auto filterId = filter->nodeID;

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = doc.addLane(track, "filter-uuid", "cutoff", makeRange());
    ASSERT_TRUE(lane.isValid());
    ASSERT_FALSE(TimelineReconciler::reconcile(doc, graph));
    ASSERT_FALSE(doc.getLane(lane)->orphaned);

    AppUndoManager undoManager;
    undoManager.recordStructuralChange(graph, [&] { graph.removeNode(filterId); });
    ASSERT_EQ(graph.getNodeForId(filterId), nullptr);

    ASSERT_TRUE(TimelineReconciler::reconcile(doc, graph));
    ASSERT_TRUE(doc.getLane(lane)->orphaned);

    ASSERT_TRUE(undoManager.undo()); // applySnapshotPreservingNodes restores the node with its uuid
    ASSERT_NE(graph.getNodeForId(filterId), nullptr);

    EXPECT_TRUE(TimelineReconciler::reconcile(doc, graph));
    EXPECT_FALSE(doc.getLane(lane)->orphaned);
}

// =============================================================================
// 8. RebindLaneRules
// =============================================================================

TEST_F(TimelineReconciliationTest, RebindLaneRules) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto laneA = doc.addLane(track, "uuid-a", "cutoff", makeRange());
    const auto laneB = doc.addLane(track, "uuid-b", "cutoff", makeRange());
    const auto laneC = doc.addLane(track, "missing-uuid", "resonance", makeRange());
    ASSERT_TRUE(laneA.isValid());
    ASSERT_TRUE(laneB.isValid());
    ASSERT_TRUE(laneC.isValid());

    ASSERT_TRUE(TimelineReconciler::reconcile(doc, graph)); // orphans laneA/B/C against an empty graph
    ASSERT_TRUE(doc.getLane(laneC)->orphaned);

    // -- rebind to a live uuid clears orphan and resolves after the next reconcile --
    ASSERT_TRUE(doc.rebindLane(laneC, "uuid-live"));
    EXPECT_EQ(doc.getLane(laneC)->nodeUuid, "uuid-live");
    EXPECT_FALSE(doc.getLane(laneC)->orphaned) << "cleared optimistically by rebindLane itself";

    auto node = graph.addNode(std::make_unique<OscillatorModule>());
    node->properties.set("uuid", "uuid-live");
    TimelineReconciler::reconcile(doc, graph);
    EXPECT_FALSE(doc.getLane(laneC)->orphaned) << "and still resolves after an actual reconcile";

    // -- rebind creating a duplicate (uuid, paramId) pair is rejected --
    const auto revisionBeforeDup = doc.getRevision();
    const int callsBeforeDup = listener.calls;
    EXPECT_FALSE(doc.rebindLane(laneA, "uuid-b")) << "(uuid-b, cutoff) is already laneB's identity";
    EXPECT_EQ(doc.getLane(laneA)->nodeUuid, "uuid-a") << "rejected: untouched";
    EXPECT_EQ(doc.getRevision(), revisionBeforeDup);
    EXPECT_EQ(listener.calls, callsBeforeDup);

    // -- rebind of an unknown LaneId is rejected --
    EXPECT_FALSE(doc.rebindLane(LaneId{999999}, "whatever"));
    EXPECT_EQ(doc.getRevision(), revisionBeforeDup);
    EXPECT_EQ(listener.calls, callsBeforeDup);
}

// =============================================================================
// 9. BundleLoadSetsOrphanFlags
// =============================================================================

TEST(TimelineReconciliationBundleTest, BundleLoadSetsOrphanFlags) {
    auto root =
        juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("agentsynth-timeline-reconcile-tests");
    root.deleteRecursively();
    root.createDirectory();
    auto dir = root.getChildFile("Bundle.agsproj");

    juce::AudioProcessorGraph graph;
    auto osc = graph.addNode(std::make_unique<OscillatorModule>());
    osc->properties.set("uuid", "real-node-uuid");

    TimelineDoc timeline;
    const auto track = timeline.addTrack(TrackKind::Midi, "Lead");
    const auto realLane = timeline.addLane(track, "real-node-uuid", "freq", makeRange());
    const auto fakeLane = timeline.addLane(track, "not-in-this-patch-uuid", "cutoff", makeRange());
    ASSERT_TRUE(realLane.isValid());
    ASSERT_TRUE(fakeLane.isValid());

    PatchDocument patchDocument;
    ASSERT_TRUE(ProjectBundle::save(dir, graph, timeline, patchDocument).ok);

    juce::AudioProcessorGraph freshGraph;
    TimelineDoc freshTimeline;
    PatchDocument freshPatchDocument;
    auto result = ProjectBundle::load(dir, freshGraph, freshTimeline, freshPatchDocument);
    ASSERT_TRUE(result.ok) << result.message;

    auto* loadedReal = freshTimeline.getLane(realLane);
    auto* loadedFake = freshTimeline.getLane(fakeLane);
    ASSERT_NE(loadedReal, nullptr) << "orphan handling never deletes";
    ASSERT_NE(loadedFake, nullptr) << "orphan handling never deletes";
    EXPECT_FALSE(loadedReal->orphaned) << "bound to a real node in the same bundle";
    EXPECT_TRUE(loadedFake->orphaned) << "bound to a uuid absent from the patch";

    root.deleteRecursively();
}

// =============================================================================
// 10. OrphanFlagIsNotSerialised
// =============================================================================

TEST_F(TimelineReconciliationTest, OrphanFlagIsNotSerialised) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = doc.addLane(track, "missing-uuid", "cutoff", makeRange());
    ASSERT_TRUE(lane.isValid());
    ASSERT_TRUE(TimelineReconciler::reconcile(doc, graph)); // graph empty: orphans it
    ASSERT_TRUE(doc.getLane(lane)->orphaned);
    ASSERT_TRUE(doc.getTrack(track) != nullptr);

    const juce::var savedVar = doc.toVar();
    const juce::String json = juce::JSON::toString(savedVar);
    EXPECT_FALSE(json.contains("orphaned")) << "orphaned is runtime-only and must never reach the wire format";

    TimelineDoc reloaded;
    ASSERT_TRUE(reloaded.fromVar(savedVar));
    ASSERT_NE(reloaded.getLane(lane), nullptr);
    EXPECT_FALSE(reloaded.getLane(lane)->orphaned) << "fromVar always starts false, whatever the source doc claimed";
}
