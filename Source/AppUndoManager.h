#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>

namespace synth {
class TimelineDoc; // Forward declaration (Source/Timeline/TimelineDoc.h)
}

/**
 * @class AppUndoManager
 * @brief Thin wrapper around juce::UndoManager with convenience methods for
 *        structural changes, parameter edits, and module repositioning.
 */
class GraphEditor; // Forward declaration

class AppUndoManager {
public:
    AppUndoManager();

    void setGraphEditor(GraphEditor* ge) { graphEditor = ge; }

    juce::UndoManager& getUndoManager() { return undoManager; }

    /**
     * @brief Records a graph-structural mutation (add/remove module, connect/disconnect).
     *
     * Captures before/after JSON snapshots of the graph and pushes a SnapshotAction.
     * The mutation lambda performs the actual graph change.
     * postRestore is called after undo/redo to refresh UI (e.g., updateComponents()).
     *
     * @param graph Reference to the audio processor graph
     * @param mutation Lambda that performs the actual graph mutation
     * @param postRestore Lambda called after undo/redo to refresh UI
     */
    void recordStructuralChange(juce::AudioProcessorGraph& graph, std::function<void()> mutation);

    /**
     * @brief Records a parameter value change.
     *
     * Creates a ParameterChangeAction that can undo/redo individual parameter edits.
     *
     * @param graph Reference to the audio processor graph
     * @param nodeId The node ID containing the parameter
     * @param paramId The parameter ID being changed
     * @param oldValue The previous parameter value
     * @param newValue The new parameter value
     */
    void recordParameterChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                               const juce::String& paramId, float oldValue, float newValue);

    /**
     * @brief Records a module position change (drag on the canvas).
     *
     * Creates a PositionChangeAction that can undo/redo module repositioning.
     * postRestore is called after undo/redo to refresh the UI.
     *
     * @param graph Reference to the audio processor graph
     * @param nodeId The module being moved
     * @param oldX Previous X position
     * @param oldY Previous Y position
     * @param newX New X position
     * @param newY New Y position
     * @param postRestore Lambda called after undo/redo to refresh UI
     */
    void recordPositionChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId, int oldX,
                              int oldY, int newX, int newY, std::function<void()> postRestore);

    /**
     * @brief Records an AI-applied patch (Apply / Merge on a patch card) as an undoable snapshot.
     *
     * Same snapshot strategy as recordStructuralChange, but the caller supplies its own
     * preRestore/postRestore hooks so the AI listener notifications (aiPatchAboutToApply /
     * aiPatchApplied) fire on undo and redo as well as on the initial apply — otherwise the
     * graph editor keeps stale ModuleComponents pointing at freed VisualBuffers.
     *
     * If the mutation reports failure nothing is pushed, so an invalid patch leaves no
     * no-op entry on the undo stack.
     *
     * @param graph Reference to the audio processor graph
     * @param actionName Human-readable transaction name (e.g. "AI patch" / "AI merge")
     * @param mutation Lambda that applies the patch; returns false if it did not apply
     * @param preRestore Lambda called before the graph is rebuilt on undo/redo
     * @param postRestore Lambda called after the graph is rebuilt on undo/redo
     * @return true if the mutation succeeded and an undo entry was pushed
     */
    bool recordAIPatch(juce::AudioProcessorGraph& graph, const juce::String& actionName, std::function<bool()> mutation,
                       std::function<void()> preRestore, std::function<void()> postRestore);

    // Snapshot-based parameter/position change recording.
    // Call captureBeforeState at gesture start, then pushSnapshotFromCapture at gesture end.
    void captureBeforeState(juce::AudioProcessorGraph& graph);
    void pushSnapshotFromCapture(juce::AudioProcessorGraph& graph);

    /**
     * @brief Records a timeline-only mutation (add/remove track, clip, note, lane, breakpoint, ...)
     *        as an undoable snapshot, on the SAME shared undo stack as the graph's own changes.
     *
     * Timeline undo is a deliberately SEPARATE action type (TimelineSnapshotAction) from the graph's
     * SnapshotAction, carrying only the timeline's before/after JSON. Folding timeline JSON into every
     * graph snapshot would multiply the size of every existing undo step — parameter tweaks, module
     * drags, AI patches — that never touch the timeline at all. Both action types are pushed through
     * this one juce::UndoManager, so the user has ONE undo stack and Cmd+Z stays chronological across
     * the graph and the timeline, however the two are interleaved.
     *
     * Captures doc.toVar() before, runs the mutation, then captures toVar() after; if the two
     * serialisations are identical (the mutation was rejected by the doc, or was a genuine no-op),
     * nothing is pushed and this returns false — a no-op mutation must not create an undo step.
     *
     * Lifetime note: exactly like SnapshotAction holding the graph, the pushed TimelineSnapshotAction
     * holds a reference to `doc` for as long as it sits on the undo stack — `doc` must outlive this
     * AppUndoManager, or clearUndoHistory() must run before the doc is destroyed.
     *
     * @param doc Reference to the timeline document.
     * @param mutation Lambda that performs the actual timeline mutation.
     * @return true if the mutation changed the doc and an undo entry was pushed, false if it was a no-op.
     */
    bool recordTimelineChange(synth::TimelineDoc& doc, const std::function<void()>& mutation);

    /**
     * @brief Records a mutation that may touch BOTH the graph and the timeline in a single gesture
     *        (the canonical case: deleting a module a timeline lane is bound to) as ONE undo step.
     *
     * Begins a single transaction, captures graph + timeline "before" state, runs the mutation once,
     * captures both "after" states, then pushes a graph SnapshotAction and/or a TimelineSnapshotAction
     * — only for the domain(s) that actually changed — inside that one transaction, so a single
     * undo()/redo() reverts or re-applies both together, never half of the combined edit.
     *
     * Reuses the exact same pre/post-restore lambda plumbing recordStructuralChange gives the graph's
     * SnapshotAction (detach module components before a rebuild, reconcile them afterwards).
     *
     * @param graph Reference to the audio processor graph.
     * @param doc Reference to the timeline document. Same lifetime contract as recordTimelineChange.
     * @param mutation Lambda that performs the combined mutation.
     * @return true if either domain changed and a transaction was pushed, false if neither did.
     */
    bool recordCombinedChange(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc,
                              const std::function<void()>& mutation);

    /**
     * @brief Hooks fired around EVERY restore this manager performs on undo/redo — the graph's
     *        SnapshotAction and the timeline's TimelineSnapshotAction alike.
     *
     * Distinct from the pre/post-restore lambdas SnapshotAction already carries: those are the
     * GraphEditor's component lifecycle (detach before processors are freed, reconcile after), and
     * the "pre" half of that pair deliberately fires LAZILY — a parameter-only undo frees nothing,
     * so it never runs. These two always fire, in every case, which is what a caller needs for:
     *
     *  - `beforeRestore` — opening an AutomationRecorder::ScopedProgrammaticApply, so the parameter
     *    writes a restore performs are never mistaken for a user's gesture. A parameter-only undo
     *    is exactly the case that writes parameters, so hanging this off the lazy hook would miss it.
     *  - `afterRestore` — re-running the timeline's binding reconciliation + publish. A graph
     *    restore can strand a track/lane binding, and a timeline restore comes back out of
     *    TimelineDoc::fromVar with every orphan flag reset to false (it is runtime-derived state),
     *    so BOTH domains need the same pass.
     *
     * Installed once by the app-level owner (MainComponent). Actions capture this manager, not the
     * callbacks, so hooks installed after an action was pushed still apply to it.
     */
    void setRestoreHooks(std::function<void()> beforeRestore, std::function<void()> afterRestore);

    // Convenience methods that delegate to undoManager
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }
    bool undo() { return undoManager.undo(); }
    bool redo() { return undoManager.redo(); }
    void clearUndoHistory() { undoManager.clearUndoHistory(); }
    void beginNewTransaction() { undoManager.beginNewTransaction(); }

    // Called by the undoable actions this manager creates — never by anything else.
    void fireBeforeRestore() {
        if (beforeRestore_)
            beforeRestore_();
    }
    void fireAfterRestore() {
        if (afterRestore_)
            afterRestore_();
    }

private:
    GraphEditor* graphEditor = nullptr;
    juce::UndoManager undoManager{30000000, 50}; // 30MB limit, 50 min transactions
    juce::var capturedBeforeState;

    // See setRestoreHooks(). Safe for an action to hold `this` and call through these: every action
    // lives inside `undoManager`, which is a member destroyed with this object.
    std::function<void()> beforeRestore_;
    std::function<void()> afterRestore_;

    // Builds a graph SnapshotAction wired to graphEditor's detach/reattach lifecycle — the
    // pre/post-restore lambda plumbing recordStructuralChange, pushSnapshotFromCapture, and the
    // graph half of recordCombinedChange all share, so it isn't duplicated three times.
    juce::UndoableAction* createGraphSnapshotAction(juce::AudioProcessorGraph& graph, const juce::var& beforeState,
                                                    const juce::var& afterState);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppUndoManager)
};
