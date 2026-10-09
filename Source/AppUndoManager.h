#pragma once

#include "AudioEngine/GraphSnapshotCache.h"
#include "Mixer/MixerPanLaw.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_data_structures/juce_data_structures.h>
#include <memory>

namespace synth {
class TimelineDoc;          // Forward declaration (Source/Timeline/TimelineDoc/TimelineDoc.h)
class MacroSet;             // Forward declaration (Source/MacroSet.h)
class MidiRemoteProjectDoc; // Forward declaration (Source/MidiRemote/RemoteModel.h)
class MixerViewDoc;         // Forward declaration (Source/Mixer/MixerViewDoc.h)
struct TransportDoc;        // Forward declaration (Source/Transport/TransportDoc.h)
} // namespace synth

bool timelineRestoreDropsTrack(const synth::TimelineDoc& doc, const juce::var& state);

/**
 * @class AppUndoManager
 * @brief Thin wrapper around juce::UndoManager with convenience methods for
 *        structural changes, parameter edits, and module repositioning.
 */
class GraphEditor; // Forward declaration

class AppUndoManager {
public:
    AppUndoManager();

    void setGraphEditor(GraphEditor* ge);

    juce::UndoManager& getUndoManager() { return undoManager; }

    /**
     * @brief Records a graph-structural mutation (add/remove module, connect/disconnect).
     *
     * Captures before/after JSON snapshots of the graph and pushes a SnapshotAction.
     * The mutation lambda performs the actual graph change.
     *
     * @param graph Reference to the audio processor graph
     * @param mutation Lambda that performs the actual graph mutation
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

    /** Records an already-applied node extra-state edit; undo/redo replay before/after via setExtraState. */
    void recordNodeExtraStateChange(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID nodeId,
                                    const juce::var& beforeExtraState, const juce::var& afterExtraState);

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

    /** @brief Consumes a captureBeforeState() capture for recordGraphAndMacroChange's
     *  `graphBeforeOverride` instead of the plain snapshot path — see the .cpp definition
     *  for why. Clears the capture as a side effect; void if nothing was captured. */
    juce::var takeCapturedGraphBeforeState();

    /** Pushes one graph undo step from an earlier graphToJSON to now; false (none) when they match. */
    bool recordGraphChangeSince(juce::AudioProcessorGraph& graph, const juce::var& beforeState);

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
     * @brief Records a project-level MIDI Remote assignment change (create via Learn, edit,
     *        delete, re-link) as an undoable snapshot, on the SAME shared undo stack as the
     *        graph's own changes (docs/control/midi-remote.md#undo).
     *
     * Unlike recordTimelineChange, this does NOT run a mutation lambda itself — the caller
     * supplies before/after `juce::var` (typically doc.toVar() around its own edit). Scope
     * (docs/control/midi-remote.md#undo): the PROJECT document only — never call this for a
     * ControllerProfileStore edit. See the .cpp for the no-op check and the lifetime note.
     *
     * @param doc Reference to the MIDI Remote project document.
     * @param beforeJson doc.toVar() captured before the edit.
     * @param afterJson doc.toVar() captured after the edit.
     * @param postRestore Optional lambda called after undo/redo restores `doc`; NOT called for the
     *        initial edit itself (mirrors MidiRemoteSnapshotAction's firstPerform skip).
     * @return true if an undo entry was pushed, false if the two snapshots were identical.
     */
    bool recordMidiRemoteChange(synth::MidiRemoteProjectDoc& doc, const juce::var& beforeJson,
                                const juce::var& afterJson, std::function<void()> postRestore = {});

    /** One undo step for a mixer pin/hide edit, from before/after `toVar()` snapshots; `doc` must outlive the step. */
    bool recordMixerViewChange(synth::MixerViewDoc& doc, const juce::var& beforeJson, const juce::var& afterJson,
                               std::function<void()> postRestore = {});

    /** Records a mixer-pan-law change as an undoable step, same firstPerform convention as
     *  recordParameterChange -- see the .cpp definition and docs/mixer/mixer.md#pan-law. */
    void recordMixerPanLawChange(std::function<void(synth::MixerPanLaw)> apply, synth::MixerPanLaw before,
                                 synth::MixerPanLaw after);

    /** One undo step for a tempo / time signature / loop edit the caller already applied; a no-op when equal. */
    void recordTransportChange(std::function<void(const synth::TransportDoc&)> apply, const synth::TransportDoc& before,
                               const synth::TransportDoc& after);

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

    /** recordCombinedChange's shape for graph + MidiRemoteProjectDoc (see the .cpp).
     *  `postRestore` mirrors recordMidiRemoteChange's own. `macros` may be null (graph + doc only). */
    bool recordGraphAndMidiRemoteChange(juce::AudioProcessorGraph& graph, synth::MidiRemoteProjectDoc& doc,
                                        const std::function<void()>& mutation, std::function<void()> postRestore = {},
                                        synth::MacroSet* macros = nullptr);

    /**
     * @brief Records a mutation that may touch the graph and/or a synth::MacroSet as ONE undo
     *        step (the canonical case: deleting a collapsed macro card, which removes both its
     *        member nodes and the now-empty macro).
     *
     * Same shape as recordCombinedChange (graph + TimelineDoc): begins one transaction, captures
     * both "before" states, runs the mutation once, captures both "after" states, then pushes a
     * graph SnapshotAction and/or a MacroSnapshotAction — only for the domain(s) that actually
     * changed — inside that one transaction. A plain group/ungroup/rename/recolour/collapse
     * never touches the graph, so only the MacroSnapshotAction is pushed; deleting a macro's
     * members pushes both, and one undo() restores the nodes AND the macro's membership
     * together.
     *
     * @param graph Reference to the audio processor graph.
     * @param macros Reference to the macro set.
     * @param mutation Lambda that performs the combined mutation.
     * @param graphBeforeOverride Optional graph "before" override for a caller whose live
     *        gesture already wrote intermediate state into the graph — see the .cpp definition.
     * @param macrosBeforeOverride The same for the macro set (a drag that changed membership live).
     * @return true if either domain changed and a transaction was pushed, false if neither did.
     */
    bool recordGraphAndMacroChange(juce::AudioProcessorGraph& graph, synth::MacroSet& macros,
                                   const std::function<void()>& mutation, const juce::var& graphBeforeOverride = {},
                                   const juce::var& macrosBeforeOverride = {});

    /**
     * @brief Records a mutation that may touch the graph, the TimelineDoc, AND a synth::MacroSet all
     *        in ONE undo step (the canonical case: "+ Track -> Audio Track", which creates a
     *        Track Audio node plus its default insert chain (graph), the new track (timeline), and
     *        the collapsed macro boxing the chain under the track's name (macros), all as a single
     *        gesture a single Cmd+Z has to remove entirely).
     *
     * Same shape as recordCombinedChange / recordGraphAndMacroChange: begins one transaction,
     * captures all three "before" states, runs the mutation once, captures all three "after" states,
     * then pushes whichever action(s) the changed domain(s) need. The graph+macro half is pushed
     * through the SAME pushGraphAndMacroActions() helper recordGraphAndMacroChange uses, so a
     * graph+macro combination that needs the single GraphAndMacroSnapshotAction (see that class's own
     * comment for why a fixed two-action push order can't get both undo AND redo right there) gets it
     * here too; a TimelineSnapshotAction follows if the timeline changed, exactly as
     * recordCombinedChange pushes its own. No beginNewTransaction between the pushes, so one
     * undo()/redo() reverts or re-applies every domain that changed, together.
     *
     * @param graph Reference to the audio processor graph.
     * @param doc Reference to the timeline document. Same lifetime contract as recordTimelineChange.
     * @param macros Reference to the macro set.
     * @param mutation Lambda that performs the combined mutation.
     * @param midiRemoteDoc Optional fourth domain; null (default) skips it -- see the .cpp.
     * @param midiRemotePostRestore Passed through when midiRemoteDoc is set.
     * @return true if any domain changed and a transaction was pushed, false if none did.
     */
    bool recordGraphTimelineAndMacroChange(juce::AudioProcessorGraph& graph, synth::TimelineDoc& doc,
                                           synth::MacroSet& macros, const std::function<void()>& mutation,
                                           synth::MidiRemoteProjectDoc* midiRemoteDoc = nullptr,
                                           std::function<void()> midiRemotePostRestore = {});

    /**
     * @brief Hooks fired around EVERY restore this manager performs on undo/redo — the graph's
     *        SnapshotAction and the timeline's TimelineSnapshotAction alike.
     *
     * Installed once by the app-level owner (MainComponent). Actions capture this manager, not the
     * callbacks, so hooks installed after an action was pushed still apply to it.
     */
    void setRestoreHooks(std::function<void()> beforeRestore, std::function<void()> afterRestore);

    // Convenience methods that delegate to undoManager
    bool canUndo() const { return undoManager.canUndo(); }
    bool canRedo() const { return undoManager.canRedo(); }
    bool undo();
    bool redo();
    bool isRestoring() const noexcept { return restoring_; }
    /** True inside an undo/redo step whose after-restore hook will run (an action has fired fireBeforeRestore()). */
    bool isRestoringWithAfterHook() const noexcept { return restoring_ && stepHooksOpen_; }
    void clearUndoHistory() { undoManager.clearUndoHistory(); }
    void beginNewTransaction() { beginTransaction(); }

    /** Folds every undo step pushed while it lives into ONE: it opens a transaction and, until the outermost scope
     *  ends, every record*Change call (and beginNewTransaction) appends to that transaction instead of opening its
     *  own. For a bulk gesture that must call single-item entry points that each record their own change (delete N
     *  tracks), so one Cmd+Z undoes the whole gesture. Nests; only the outermost scope opens the transaction. */
    class ScopedUndoGroup {
    public:
        explicit ScopedUndoGroup(AppUndoManager& manager)
            : manager_(manager) {
            if (manager_.groupDepth_++ == 0)
                manager_.undoManager.beginNewTransaction();
        }
        ~ScopedUndoGroup() { --manager_.groupDepth_; }
        ScopedUndoGroup(const ScopedUndoGroup&) = delete;
        ScopedUndoGroup& operator=(const ScopedUndoGroup&) = delete;

    private:
        AppUndoManager& manager_;
    };

    /** Counts every change this manager has actually applied to the document — one per pushed
     *  action, plus one per undo/redo. Monotonic, never reset.
     *
     *  It exists because `juce::UndoManager`'s own change broadcast is ASYNCHRONOUS
     *  (`sendChangeMessage()`), which makes "am I dirty?" unanswerable from the broadcast alone: a
     *  path that edits the document and then declares it clean in the same call stack (New Patch
     *  clears the timeline and the graph, each an undoable step, before resetting the document)
     *  leaves a notification already queued, and that notification would otherwise arrive after the
     *  reset and re-dirty a brand-new document. A listener that instead compares this serial against
     *  the one captured at save/load/new time recomputes the truth on every notification, so a stale
     *  one is harmless. See `MainComponent::markDocumentClean`. */
    int getEditSerial() const { return editSerial_; }

    int getRestoreSerial() const noexcept { return restoreSerial_; }

    void fireBeforeRestore(); // called by the undoable actions this manager creates, never by anything else
    void fireAfterRestore();

    void setTrackListHooks(std::function<void()> tracksLeaving, std::function<void()> trackListSettled);
    void fireTracksLeaving(); // called by the timeline snapshot action only

private:
    // Undo or redo one step, retracting the cables it takes away.
    bool applyHistoryStep(bool redoStep);
    bool performAction(juce::UndoableAction* action); // THE one push: nothing else calls undoManager.perform()
    juce::var captureGraph(juce::AudioProcessorGraph& graph) {
        return graphSnapshots_.capture(graph); // every graph snapshot, never a fresh graphToJSON
    }

    juce::Component::SafePointer<GraphEditor> graphEditor;
    juce::UndoManager undoManager{30000000, 50}; // 30MB limit, 50 min transactions
    juce::var capturedBeforeState;
    synth::GraphSnapshotCache graphSnapshots_;
    // See getEditSerial(). Incremented by performAction() and by a successful undo/redo.
    int editSerial_ = 0;
    int restoreSerial_ = 0;
    int groupDepth_ = 0; // > 0 inside a ScopedUndoGroup: beginTransaction() opens nothing new
    // The one place a record*Change call opens its undo transaction, so a ScopedUndoGroup can fold them into one.
    void beginTransaction(const juce::String& name = {}) {
        if (groupDepth_ == 0)
            undoManager.beginNewTransaction(name);
    }
    bool restoring_ = false;
    bool stepHooksOpen_ = false, stepAfterRestorePending_ = false; // see fireBeforeRestore()
    std::shared_ptr<void> glideScope_;                             // a CardGlideAnimator::Scope, see beginRestore()
    void beginRestore();
    void endRestore(bool did);

    // See setRestoreHooks(). Safe for an action to hold `this` and call through these: every action
    // lives inside `undoManager`, which is a member destroyed with this object.
    std::function<void()> beforeRestore_;
    std::function<void()> afterRestore_;
    std::function<void()> tracksLeaving_;
    std::function<void()> trackListSettled_;
    bool tracksLeavingFired_ = false;

    // Builds a graph SnapshotAction wired to graphEditor's detach/reattach lifecycle — the
    // pre/post-restore lambda plumbing recordStructuralChange, pushSnapshotFromCapture, and the
    // graph half of recordCombinedChange all share, so it isn't duplicated three times.
    juce::UndoableAction* createGraphSnapshotAction(juce::AudioProcessorGraph& graph, const juce::var& beforeState,
                                                    const juce::var& afterState);

    // The graph+macro push logic recordGraphAndMacroChange and recordGraphTimelineAndMacroChange both
    // need, factored out so it isn't duplicated: picks GraphAndMacroSnapshotAction when both domains
    // changed, createGraphSnapshotAction()/MacroSnapshotAction when only one did, and pushes nothing
    // when neither did (callers only call this when at least one of graphChanged/macrosChanged holds).
    void pushGraphAndMacroActions(juce::AudioProcessorGraph& graph, synth::MacroSet& macros,
                                  const juce::var& graphBefore, const juce::var& graphAfter,
                                  const juce::var& macrosBefore, const juce::var& macrosAfter, bool graphChanged,
                                  bool macrosChanged);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppUndoManager)
};
