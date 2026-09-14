// Concern: P8-2 -- the edit serial behind MainComponent's dirty flag (advances exactly once
// per real edit, synchronously, including through undo/redo, and never for a no-op push or a
// refused undo).
#include "Project/UndoRedo/UndoRedoTestFixture.h"

// =============================================================================
// P8-2: the edit serial behind MainComponent's dirty flag.
//
// The dirty flag cannot be driven by juce::UndoManager's change broadcast alone, because that
// broadcast is ASYNC: a caller that edits the document and then declares it clean in the same call
// stack (New Patch clears the timeline and the graph, each an undoable step, before resetting)
// leaves a notification queued that would arrive afterwards and re-dirty a brand-new document.
// getEditSerial() is what lets the listener recompute instead of blindly setting — so what these
// tests pin is that the serial moves for exactly the things that change the document, and that it
// is readable SYNCHRONOUSLY, with no message loop involved.
// =============================================================================

TEST_F(UndoRedoTest, EditSerialStartsAtZeroAndAdvancesOncePerPush) {
    EXPECT_EQ(undoManager.getEditSerial(), 0) << "a manager that has applied nothing has a zero serial";

    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    undoManager.captureBeforeState(graph);
    auto* fine = findParam(osc->getProcessor(), "fine");
    ASSERT_NE(fine, nullptr);
    fine->setValueNotifyingHost(0.75f);
    undoManager.pushSnapshotFromCapture(graph);

    const int afterFirst = undoManager.getEditSerial();
    EXPECT_GT(afterFirst, 0) << "a pushed action must advance the serial";

    undoManager.captureBeforeState(graph);
    fine->setValueNotifyingHost(0.25f);
    undoManager.pushSnapshotFromCapture(graph);
    EXPECT_GT(undoManager.getEditSerial(), afterFirst) << "a second edit must advance it again";
}

// A no-op "edit" pushes nothing, so it must not move the serial either — otherwise a gesture that
// ended exactly where it started would mark a saved project dirty.
TEST_F(UndoRedoTest, EditSerialIgnoresAPushThatChangedNothing) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    juce::ignoreUnused(osc);
    undoManager.captureBeforeState(graph);
    undoManager.pushSnapshotFromCapture(graph); // nothing was mutated in between

    EXPECT_EQ(undoManager.getEditSerial(), 0) << "an unchanged snapshot must not count as an edit";
}

// THE property the dirty flag depends on: undo and redo move the serial FORWARD rather than back
// to a previous value. That is what makes "undo back to the state I saved" still read as dirty —
// the deliberate semantics documented in docs/architecture.md, chosen because a false "clean"
// loses work silently while a false "dirty" only costs one extra prompt.
TEST_F(UndoRedoTest, EditSerialAdvancesOnUndoAndRedoRatherThanRewinding) {
    auto* osc = graph.addNode(std::make_unique<OscillatorModule>()).get();
    undoManager.captureBeforeState(graph);
    auto* fine = findParam(osc->getProcessor(), "fine");
    ASSERT_NE(fine, nullptr);
    fine->setValueNotifyingHost(0.75f);
    undoManager.pushSnapshotFromCapture(graph);

    const int afterEdit = undoManager.getEditSerial();
    ASSERT_TRUE(undoManager.undo());
    const int afterUndo = undoManager.getEditSerial();
    EXPECT_GT(afterUndo, afterEdit) << "undo moves the document, so it must advance the serial";

    ASSERT_TRUE(undoManager.redo());
    EXPECT_GT(undoManager.getEditSerial(), afterUndo) << "redo moves it too — the serial never rewinds";
}

// A refused undo (nothing on the stack) is not a document change, so it must not advance anything.
TEST_F(UndoRedoTest, EditSerialIgnoresAnUndoThatDidNothing) {
    ASSERT_FALSE(undoManager.canUndo());
    EXPECT_FALSE(undoManager.undo());
    EXPECT_EQ(undoManager.getEditSerial(), 0);
}
