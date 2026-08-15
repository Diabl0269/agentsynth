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
 * Today, only ProjectBundle::load calls this — right after the trusted applyJSONToGraph and the
 * timeline's own fromVar, so a freshly opened project shows correct orphan flags before the user
 * touches anything.
 *
 * Once the app owns a live TimelineDoc alongside its graph (TL3+), this must ALSO run after
 * every one of AppUndoManager's graph-restoring choke points — anywhere a node can appear or
 * disappear is a place a binding can start or stop resolving:
 *  - After AIStateMapper::applyJSONToGraph (AI patch apply/merge, and the clearExisting=true
 *    fallback SnapshotAction::restore() takes when the node-preserving path below declines).
 *  - After AIStateMapper::applySnapshotPreservingNodes (SnapshotAction's preferred undo/redo
 *    path — the node-preserving restore itself keeps a kept node's "uuid" untouched and adopts
 *    the snapshot's uuid onto every re-created node, but a node the snapshot doesn't contain is
 *    still gone, and anything bound to it is now orphaned).
 *  - After any direct node delete outside the undo manager entirely (a "delete module" gesture
 *    that doesn't go through recordStructuralChange/recordCombinedChange).
 *
 * None of those call sites exist yet — there is no live TimelineDoc wired into the running app
 * today — which is why this struct's only current caller is ProjectBundle::load.
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
