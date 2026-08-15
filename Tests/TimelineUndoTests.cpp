#include "AI/AIStateMapper.h"
#include "AppUndoManager.h"
#include "Modules/FilterModule.h"
#include "Modules/OscillatorModule.h"
#include "Timeline/TimelineDoc.h"
#include <gtest/gtest.h>
#include <juce_audio_processors/juce_audio_processors.h>

// TL2-5: timeline undo lives on the SAME juce::UndoManager as the graph's own undo (AppUndoManager),
// through a separate TimelineSnapshotAction rather than being folded into the graph's SnapshotAction —
// see AppUndoManager::recordTimelineChange / recordCombinedChange for why. These tests check three
// things: a timeline-only edit round-trips exactly and never touches the graph; a graph-only edit
// never touches the timeline; and a combined edit lands as exactly one step on the shared stack.

using synth::AutomationLane;
using synth::BreakpointCurve;
using synth::TimelineDoc;
using synth::TrackId;
using synth::TrackKind;

namespace {

synth::MidiNote makeNote(double startBeat, int pitch, double lengthBeats = 1.0, int velocity = 100, int channel = 1) {
    synth::MidiNote note;
    note.startBeat = startBeat;
    note.lengthBeats = lengthBeats;
    note.pitch = pitch;
    note.velocity = velocity;
    note.channel = channel;
    return note;
}

AutomationLane::RangeSnapshot makeRange(float minValue, float maxValue, float defaultValue) {
    AutomationLane::RangeSnapshot range;
    range.minValue = minValue;
    range.maxValue = maxValue;
    range.defaultValue = defaultValue;
    return range;
}

// Deep equality by serialised form, same convention TimelineDocTests.cpp uses.
juce::String dump(const TimelineDoc& doc) { return juce::JSON::toString(doc.toVar()); }

juce::String dumpGraph(juce::AudioProcessorGraph& g) {
    return juce::JSON::toString(synth::AIStateMapper::graphToJSON(g));
}

} // namespace

class TimelineUndoTest : public ::testing::Test {
protected:
    juce::AudioProcessorGraph graph;
    AppUndoManager undoManager;
    TimelineDoc doc;

    void SetUp() override { graph.clear(); }
};

// =============================================================================
// 1. UndoRedoRestoresExactTimeline
// =============================================================================

TEST_F(TimelineUndoTest, UndoRedoRestoresExactTimeline) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto clip = doc.addClip(track, 0.0, 4.0, "A");
    doc.addNote(clip, makeNote(0.0, 60));
    const auto lane = doc.addLane(track, "uuid-osc", "fine", makeRange(0.0f, 1.0f, 0.5f));
    ASSERT_TRUE(lane.isValid());

    const juce::String beforeJson = dump(doc);
    ASSERT_FALSE(undoManager.canUndo());

    ASSERT_TRUE(undoManager.recordTimelineChange(doc, [&] {
        doc.addNote(clip, makeNote(2.0, 64));
        doc.addBreakpoint(lane, 1.0, 0.75);
    }));

    const juce::String afterJson = dump(doc);
    EXPECT_NE(beforeJson, afterJson) << "the mutation should have changed the doc";
    ASSERT_TRUE(undoManager.canUndo());

    const auto revisionBeforeUndo = doc.getRevision();
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), beforeJson) << "undo must restore the exact pre-mutation serialisation";
    EXPECT_GT(doc.getRevision(), revisionBeforeUndo) << "fromVar bumps the revision exactly once on restore";

    const auto revisionBeforeRedo = doc.getRevision();
    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(dump(doc), afterJson) << "redo must restore the exact post-mutation serialisation";
    EXPECT_GT(doc.getRevision(), revisionBeforeRedo) << "fromVar bumps the revision exactly once on restore";
}

// =============================================================================
// 2. NoOpMutationCreatesNoUndoStep
// =============================================================================

TEST_F(TimelineUndoTest, NoOpMutationCreatesNoUndoStep) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");

    // Give the stack one real entry first, so "unchanged" below is a meaningful assertion and not
    // just an empty stack staying empty.
    ASSERT_TRUE(undoManager.recordTimelineChange(doc, [&] { doc.setTrackName(track, "Renamed"); }));
    const bool canUndoBefore = undoManager.canUndo();
    const bool canRedoBefore = undoManager.canRedo();
    ASSERT_TRUE(canUndoBefore);

    // (a) Mutation that changes nothing at all.
    EXPECT_FALSE(undoManager.recordTimelineChange(doc, [] {}));
    EXPECT_EQ(undoManager.canUndo(), canUndoBefore);
    EXPECT_EQ(undoManager.canRedo(), canRedoBefore);

    // (b) Mutation that is itself a no-op on TimelineDoc's own terms (same value already stored).
    EXPECT_FALSE(undoManager.recordTimelineChange(doc, [&] { doc.setTrackName(track, "Renamed"); }));
    EXPECT_EQ(undoManager.canUndo(), canUndoBefore);

    // (c) Mutation rejected outright by TimelineDoc (no such track).
    EXPECT_FALSE(undoManager.recordTimelineChange(doc, [&] { doc.removeTrack(TrackId{99999}); }));
    EXPECT_EQ(undoManager.canUndo(), canUndoBefore);
}

// =============================================================================
// 3. TimelineUndoDoesNotTouchTheGraph
// =============================================================================

TEST_F(TimelineUndoTest, TimelineUndoDoesNotTouchTheGraph) {
    graph.addNode(std::make_unique<OscillatorModule>());
    graph.addNode(std::make_unique<FilterModule>());
    const juce::String graphJsonBefore = dumpGraph(graph); // stamps uuids
    const int nodeCountBefore = graph.getNumNodes();

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");

    ASSERT_TRUE(undoManager.recordTimelineChange(doc, [&] { doc.setTrackName(track, "Renamed"); }));

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), nodeCountBefore);
    EXPECT_EQ(dumpGraph(graph), graphJsonBefore) << "a timeline-only undo must not touch the graph";

    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(graph.getNumNodes(), nodeCountBefore);
    EXPECT_EQ(dumpGraph(graph), graphJsonBefore) << "a timeline-only redo must not touch the graph either";
}

// =============================================================================
// 4. GraphUndoDoesNotTouchTheTimeline
// =============================================================================

TEST_F(TimelineUndoTest, GraphUndoDoesNotTouchTheTimeline) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    doc.addClip(track, 0.0, 4.0, "A");
    const juce::String timelineJsonBefore = dump(doc);
    const auto revisionBefore = doc.getRevision();

    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 1);
    EXPECT_EQ(dump(doc), timelineJsonBefore) << "recordStructuralChange (existing API) must not touch the timeline";
    EXPECT_EQ(doc.getRevision(), revisionBefore);

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 0);
    EXPECT_EQ(dump(doc), timelineJsonBefore) << "undoing a graph-only change must not touch the timeline";
    EXPECT_EQ(doc.getRevision(), revisionBefore);
}

// =============================================================================
// 5. CombinedChangeIsOneUndoStep
// =============================================================================

TEST_F(TimelineUndoTest, CombinedChangeIsOneUndoStep) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    synth::AIStateMapper::graphToJSON(graph); // stamps a uuid onto every live node, including osc
    const juce::String oscUuid = osc->properties["uuid"].toString();
    ASSERT_TRUE(oscUuid.isNotEmpty());
    const auto oscNodeId = osc->nodeID;

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    const auto lane = doc.addLane(track, oscUuid, "fine", makeRange(0.0f, 1.0f, 0.5f));
    ASSERT_TRUE(lane.isValid());

    // An unrelated earlier undo step, so "one undo reverts the combined edit" can be told apart from
    // "one undo reverts everything back to the start".
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<FilterModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 2);

    // The canonical cross-domain edit: delete the module a lane is bound to, and drop the lane too.
    ASSERT_TRUE(undoManager.recordCombinedChange(graph, doc, [&] {
        graph.removeNode(oscNodeId);
        doc.removeLane(lane);
    }));
    EXPECT_EQ(graph.getNumNodes(), 1);
    EXPECT_EQ(doc.getLane(lane), nullptr);

    // ONE undo restores BOTH halves.
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 2) << "the combined undo must restore the removed node";
    auto* restoredOsc = graph.getNodeForId(oscNodeId);
    ASSERT_NE(restoredOsc, nullptr);
    EXPECT_EQ(restoredOsc->properties["uuid"].toString(), oscUuid) << "identity must be restored, not regenerated";
    EXPECT_NE(doc.getLane(lane), nullptr) << "the combined undo must restore the lane too";

    // It was exactly one step: canRedo is true, and a second undo reverts the EARLIER (Filter-add)
    // step whole — not half of the combined edit.
    EXPECT_TRUE(undoManager.canRedo());
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 1) << "the second undo should revert the unrelated Filter-add step";
    EXPECT_NE(doc.getLane(lane), nullptr) << "the second undo must not have touched the combined step's timeline half";

    // Redo replays both steps forward again.
    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(graph.getNumNodes(), 2);
    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(graph.getNumNodes(), 1);
    EXPECT_EQ(doc.getLane(lane), nullptr);
}

// =============================================================================
// 6. CombinedChangeSkipsUnchangedDomain
// =============================================================================

TEST_F(TimelineUndoTest, CombinedChangeSkipsUnchangedDomain) {
    // --- Timeline-only combined edit: undoing it must leave the graph untouched. ---
    graph.addNode(std::make_unique<OscillatorModule>());
    const juce::String graphJsonBaseline = dumpGraph(graph);
    const int nodeCountBaseline = graph.getNumNodes();

    const auto track = doc.addTrack(TrackKind::Midi, "Lead");
    undoManager.clearUndoHistory();

    ASSERT_TRUE(undoManager.recordCombinedChange(graph, doc, [&] { doc.setTrackName(track, "Renamed"); }));
    EXPECT_TRUE(undoManager.canUndo());

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), nodeCountBaseline);
    EXPECT_EQ(dumpGraph(graph), graphJsonBaseline) << "a timeline-only combined change must not disturb the graph";
    EXPECT_EQ(doc.getTrack(track)->name, juce::String("Lead"));
    EXPECT_FALSE(undoManager.canUndo()) << "the skipped domain must not have pushed a second, empty step";

    ASSERT_TRUE(undoManager.redo());
    undoManager.clearUndoHistory();

    // --- Graph-only combined edit: undoing it must leave the timeline untouched. ---
    const juce::String timelineJsonBaseline = dump(doc);
    const auto revisionBaseline = doc.getRevision();

    ASSERT_TRUE(undoManager.recordCombinedChange(graph, doc, [&] { graph.addNode(std::make_unique<FilterModule>()); }));
    EXPECT_TRUE(undoManager.canUndo());

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), nodeCountBaseline);
    EXPECT_EQ(dump(doc), timelineJsonBaseline) << "a graph-only combined change must not disturb the timeline";
    EXPECT_EQ(doc.getRevision(), revisionBaseline);
    EXPECT_FALSE(undoManager.canUndo());
}

// =============================================================================
// 7. InterleavedDomainsUndoInOrder
// =============================================================================

TEST_F(TimelineUndoTest, InterleavedDomainsUndoInOrder) {
    const auto track = doc.addTrack(TrackKind::Midi, "Lead");

    // Step A: timeline edit.
    ASSERT_TRUE(undoManager.recordTimelineChange(doc, [&] { doc.setTrackName(track, "A"); }));
    const juce::String timelineAfterA = dump(doc);

    // Step B: graph edit.
    undoManager.recordStructuralChange(graph, [this] { graph.addNode(std::make_unique<OscillatorModule>()); });
    ASSERT_EQ(graph.getNumNodes(), 1);

    // Step C: timeline edit.
    ASSERT_TRUE(undoManager.recordTimelineChange(doc, [&] { doc.setTrackName(track, "C"); }));
    EXPECT_EQ(doc.getTrack(track)->name, juce::String("C"));

    // Undo #1 reverts C: timeline back to its post-A state, graph untouched (still 1 node).
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), timelineAfterA);
    EXPECT_EQ(graph.getNumNodes(), 1);

    // Undo #2 reverts B: graph node gone, timeline still at its post-A state.
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(graph.getNumNodes(), 0);
    EXPECT_EQ(dump(doc), timelineAfterA);

    // Undo #3 reverts A: timeline back to its pre-edit name.
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(doc.getTrack(track)->name, juce::String("Lead"));
    EXPECT_EQ(graph.getNumNodes(), 0);

    EXPECT_FALSE(undoManager.canUndo()) << "three undos should have unwound exactly the three recorded steps";
}
