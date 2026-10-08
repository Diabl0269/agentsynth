// Concern: AppUndoManager's undo and redo of one history step: the restore bracket and hooks around it, and the
// canvas animation it triggers.
#include "AppUndoManager.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Graph/CardGlideAnimator/CardGlideAnimator.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include <algorithm>

// Undo and redo bump the edit serial for the same reason a fresh edit does: after a save, an undo moves the
// document AWAY from what is on disk, so it has to read as modified. A cable the step takes away retracts on
// the canvas rather than vanishing (GraphEditor::retractCablesGoneSince).
bool AppUndoManager::applyHistoryStep(bool redoStep) {
    juce::Component::SafePointer<GraphEditor> ge(graphEditor);
    const auto cablesBefore =
        ge != nullptr ? ge->snapshotCablesForRetract() : std::vector<graph_editor_types::VisibleCable>();
    const auto hullsBefore = ge != nullptr ? ge->snapshotPaintedHulls() : MacroHullGlide::Hulls();
    beginRestore();
    const bool did = redoStep ? undoManager.redo() : undoManager.undo();
    endRestore(did);
    if (did && ge != nullptr) {
        ge->retractCablesGoneSince(cablesBefore);
        ge->glideHullsFrom(hullsBefore); // a macro border a take-out or join moved glides back like it glided out
    }
    tracksLeavingFired_ = false;
    if (trackListSettled_)
        trackListSettled_(); // a track the step removed shrinks away and one it brought back grows in
    return did;
}

bool AppUndoManager::undo() { return applyHistoryStep(false); }

bool AppUndoManager::redo() { return applyHistoryStep(true); }

// Both undo() and redo() land here (docs/layout/animation.md "Undo and redo glide"). isRestoring() lets views glide
// rather than snap while it runs. The glide scope captures the cards' bounds before the restore and arms a slide from
// them afterwards, so the canvas moves back the way the edit moved forward. A card matches by component, or by node id
// when the restore rebuilt the cards (freeing any node tears every card down, as taking a module out of a macro does);
// a card the restore removes shrinks away and one it creates grows back (CardGlideAnimatorGhosts.cpp).
void AppUndoManager::beginRestore() {
    restoring_ = true;
    if (graphEditor != nullptr)
        glideScope_ = std::make_shared<CardGlideAnimator::Scope>(graphEditor->getCardGlide(), /*restore=*/true);
}

void AppUndoManager::endRestore(bool did) {
    if (stepAfterRestorePending_ && afterRestore_)
        afterRestore_(); // still inside the restore, as each action's own call was
    stepHooksOpen_ = stepAfterRestorePending_ = false;
    glideScope_.reset(); // arms the slide from the captured bounds to the restored ones
    restoring_ = false;
    if (did) {
        ++editSerial_;
        ++restoreSerial_;
    }
}

// Distinct from the pre/post-restore lambdas SnapshotAction already carries: those are the
// GraphEditor's component lifecycle (detach before processors are freed, reconcile after), and
// the "pre" half of that pair deliberately fires LAZILY — a parameter-only undo frees nothing,
// so it never runs. These two always fire, in every case, which is what a caller needs for:
//
//  - `beforeRestore` — opening an AutomationRecorder::ScopedProgrammaticApply, so the parameter
//    writes a restore performs are never mistaken for a user's gesture. A parameter-only undo
//    is exactly the case that writes parameters, so hanging this off the lazy hook would miss it.
//  - `afterRestore` — re-running the timeline's binding reconciliation + publish. A graph
//    restore can strand a track/lane binding, and a timeline restore comes back out of
//    TimelineDoc::fromVar with every orphan flag reset to false (it is runtime-derived state),
//    so BOTH domains need the same pass.
// Inside an undo() or redo() step the pair fires once for the whole step, however many actions it restores: before
// the first restore and after the last. A combined step (a duplicate: graph + macros, then timeline) used to reconcile
// the timeline, rebuild the mixer and republish once per action, each a whole-project pass, the first against a
// half-restored document. A restore run outside a step (juce::UndoManager driven directly) fires per action.
void AppUndoManager::fireBeforeRestore() {
    if (restoring_) {
        if (stepHooksOpen_)
            return;
        stepHooksOpen_ = true;
    }
    if (beforeRestore_)
        beforeRestore_();
}

void AppUndoManager::fireAfterRestore() {
    if (restoring_) {
        stepAfterRestorePending_ = true;
        return;
    }
    if (afterRestore_)
        afterRestore_();
}

void AppUndoManager::setRestoreHooks(std::function<void()> beforeRestore, std::function<void()> afterRestore) {
    beforeRestore_ = std::move(beforeRestore);
    afterRestore_ = std::move(afterRestore);
}

// True when restoring `state` (a TimelineDoc::toVar snapshot) into `doc` would drop a track it has now.
bool timelineRestoreDropsTrack(const synth::TimelineDoc& doc, const juce::var& state) {
    const juce::var trackList = state.getProperty("tracks", juce::var());
    const auto* tracks = trackList.getArray();
    if (tracks == nullptr)
        return false;
    for (const auto& current : doc.getTracks()) {
        const bool kept = std::any_of(tracks->begin(), tracks->end(), [&current](const juce::var& track) {
            return static_cast<juce::int64>(track.getProperty("id", juce::var())) ==
                   static_cast<juce::int64>(current.id.value);
        });
        if (!kept)
            return true;
    }
    return false;
}

// Hooks for the track list's delete and undo motion (docs/layout/animation.md "Delete and undo animation"):
// `tracksLeaving` fires once per undo/redo step, before a restore that drops a track, while its row and column are
// still on screen; `trackListSettled` fires after every undo/redo step once everything is restored and re-synced.
// MainComponent installs both and calls them itself around a track delete. getRestoreSerial() counts the steps that
// took effect, so a view that follows the document on a tick can tell a row an undo brought back from one the user just
// added.
void AppUndoManager::setTrackListHooks(std::function<void()> tracksLeaving, std::function<void()> trackListSettled) {
    tracksLeaving_ = std::move(tracksLeaving);
    trackListSettled_ = std::move(trackListSettled);
}

// Once per step: a combined step restores more than one timeline action, and the picture is of the state before the
// first.
void AppUndoManager::fireTracksLeaving() {
    if (!tracksLeaving_ || (restoring_ && tracksLeavingFired_))
        return;
    tracksLeavingFired_ = restoring_;
    tracksLeaving_();
}
