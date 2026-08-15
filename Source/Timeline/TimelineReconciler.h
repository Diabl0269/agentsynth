#pragma once

#include "TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>

namespace synth {

/**
 * @brief Bridges TimelineDoc to a real graph (TL2-6).
 *
 * TimelineDoc::reconcileBindings() is deliberately graph-agnostic — it takes a plain
 * `uuid -> bool` callback so the document model stays headless and testable without a live
 * juce::AudioProcessorGraph. TimelineReconciler is the one place that builds that callback from
 * an actual graph: it collects the "uuid" property of every live node and reconciles the doc
 * against that set. A node with no "uuid" property (or an empty one) simply resolves nothing —
 * it can neither rescue an orphaned binding nor be a valid binding target, since a binding is
 * only ever created against a uuid a node already carries.
 *
 * Message thread only — juce::AudioProcessorGraph::getNodes() and TimelineDoc are both
 * message-thread-only, exactly like everything else that touches either.
 *
 * ### Call sites
 *
 * ProjectBundle::load calls this right after the trusted applyJSONToGraph and the timeline's own
 * fromVar, so a freshly opened project shows correct orphan flags before the user touches anything.
 *
 * TL5-3 wired up the rest: MainComponent owns the running app's live TimelineDoc and calls this
 * from every place a node can appear or disappear, because each of those is a place a binding can
 * start or stop resolving. The definitive list lives in docs/architecture.md ("App wiring (TL5-3)")
 * — keep the two in step. In outline:
 *  - MainComponent::reconcileTimelineAfterGraphChange(), from preset load, factory-preset load, New
 *    Patch, .agsproj open and AIIntegrationService's post-apply notification (which also covers
 *    AIStateMapper::applyJSONToGraph's undo/redo path).
 *  - AppUndoManager::setRestoreHooks' afterRestore, around EVERY undo/redo restore — the
 *    node-preserving path (applySnapshotPreservingNodes) as well as the clearExisting=true
 *    fallback, and timeline-only restores too (fromVar always comes back with orphaned == false).
 *  - GraphEditor::onGraphStructureChanged, the catch-all for a direct node delete that goes through
 *    recordStructuralChange rather than a restore.
 */
struct TimelineReconciler {
    /**
     * @brief Reconciles `doc`'s track/lane orphan flags against `graph`'s live node uuids.
     * @return Whatever TimelineDoc::reconcileBindings returns: true if at least one orphan flag
     *         actually changed (one revision bump, one listener notification fired on `doc`);
     *         false if every flag already matched (a genuine no-op — nothing bumped, nothing
     *         fired).
     */
    static bool reconcile(TimelineDoc& doc, const juce::AudioProcessorGraph& graph);
};

} // namespace synth
