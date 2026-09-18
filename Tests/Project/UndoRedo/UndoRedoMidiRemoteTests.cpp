// AppUndoManager::recordMidiRemoteChange (docs/control/midi-remote.md#undo): a project-level MIDI Remote
// assignment change (the "midiRemote" MidiRemoteProjectDoc) is undoable through a
// MidiRemoteSnapshotAction on the SAME shared juce::UndoManager as the graph's own changes — same
// shape as Tests/Timeline/TimelineUndoTests.cpp, kept headless (no GraphEditor). Unlike
// recordTimelineChange, this method takes the caller's own before/after juce::var directly rather
// than running a mutation lambda itself.
#include "AppUndoManager.h"
#include "MidiRemote/RemoteModel.h"
#include <gtest/gtest.h>

using synth::Assignment;
using synth::MidiRemoteProjectDoc;
using synth::Target;

namespace {

Assignment makeAssignment(const juce::String& id) {
    Assignment a;
    a.id = id;
    a.control.profileId = "profile-1";
    a.control.controlId = "control-1";
    a.spec.type = synth::MessageType::cc;
    a.spec.channel = 1;
    a.spec.number = 21;
    a.target.kind = Target::Kind::parameter;
    a.target.parameter.nodeUuid = "node-1";
    a.target.parameter.paramId = "cutoff";
    return a;
}

juce::String dump(const MidiRemoteProjectDoc& doc) { return juce::JSON::toString(doc.toVar()); }

} // namespace

class UndoRedoMidiRemoteTest : public ::testing::Test {
protected:
    AppUndoManager undoManager;
    MidiRemoteProjectDoc doc;
};

TEST_F(UndoRedoMidiRemoteTest, RecordPushesExactlyOneStepForARealChange) {
    ASSERT_FALSE(undoManager.canUndo());

    const juce::var beforeJson = doc.toVar();
    doc.assignments.push_back(makeAssignment("assign-1"));
    const juce::var afterJson = doc.toVar();

    EXPECT_TRUE(undoManager.recordMidiRemoteChange(doc, beforeJson, afterJson));
    EXPECT_TRUE(undoManager.canUndo());
    EXPECT_FALSE(undoManager.canRedo());
}

TEST_F(UndoRedoMidiRemoteTest, NoOpBeforeEqualsAfterPushesNothing) {
    // Give the stack one real entry first, so "canUndo unchanged" below is a meaningful assertion
    // rather than an empty stack staying empty (same pattern as TimelineUndoTests.cpp).
    const juce::var seedBefore = doc.toVar();
    doc.assignments.push_back(makeAssignment("seed"));
    const juce::var seedAfter = doc.toVar();
    ASSERT_TRUE(undoManager.recordMidiRemoteChange(doc, seedBefore, seedAfter));
    const bool canUndoBefore = undoManager.canUndo();
    const bool canRedoBefore = undoManager.canRedo();
    ASSERT_TRUE(canUndoBefore);

    const juce::var identicalJson = doc.toVar();
    EXPECT_FALSE(undoManager.recordMidiRemoteChange(doc, identicalJson, identicalJson));
    EXPECT_EQ(undoManager.canUndo(), canUndoBefore);
    EXPECT_EQ(undoManager.canRedo(), canRedoBefore);
}

TEST_F(UndoRedoMidiRemoteTest, UndoRestoresBeforeJsonAndRedoRestoresAfterJson) {
    doc.assignments.push_back(makeAssignment("assign-existing"));
    const juce::String beforeString = dump(doc);
    const juce::var beforeJson = doc.toVar();

    doc.assignments.push_back(makeAssignment("assign-new"));
    doc.controllers.push_back({"profile-1", "Launchkey Mini MK3"});
    const juce::String afterString = dump(doc);
    const juce::var afterJson = doc.toVar();

    ASSERT_TRUE(undoManager.recordMidiRemoteChange(doc, beforeJson, afterJson));
    EXPECT_EQ(dump(doc), afterString) << "the doc already holds the post-edit state before any undo";

    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), beforeString) << "undo must restore the exact pre-edit serialisation";

    ASSERT_TRUE(undoManager.redo());
    EXPECT_EQ(dump(doc), afterString) << "redo must restore the exact post-edit serialisation";
}

TEST_F(UndoRedoMidiRemoteTest, SharesTheUndoStackWithOtherActionTypes) {
    // Two MIDI Remote edits, one undo step each, on the SAME juce::UndoManager Cmd+Z drives.
    const juce::var v0 = doc.toVar();
    doc.assignments.push_back(makeAssignment("a"));
    const juce::var v1 = doc.toVar();
    ASSERT_TRUE(undoManager.recordMidiRemoteChange(doc, v0, v1));

    doc.assignments.push_back(makeAssignment("b"));
    const juce::var v2 = doc.toVar();
    ASSERT_TRUE(undoManager.recordMidiRemoteChange(doc, v1, v2));

    EXPECT_EQ(dump(doc), juce::JSON::toString(v2));
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), juce::JSON::toString(v1));
    ASSERT_TRUE(undoManager.undo());
    EXPECT_EQ(dump(doc), juce::JSON::toString(v0));
    EXPECT_FALSE(undoManager.canUndo());
}
