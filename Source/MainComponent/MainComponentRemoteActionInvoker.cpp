// See MainComponentRemoteActionInvoker.h for what this is and why it's a standalone class.

#include "MainComponentRemoteActionInvoker.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"

MainComponentRemoteActionInvoker::MainComponentRemoteActionInvoker(juce::ApplicationCommandManager& cm,
                                                                   AudioEngine& engine, AppUndoManager& undo) noexcept
    : commandManager_(cm)
    , audioEngine_(engine)
    , undoManager_(undo) {}

// MESSAGE THREAD (called from RemoteEngine::drain()). Synchronous, exactly like a menu item or a
// keypress dispatch — never posted/async.
void MainComponentRemoteActionInvoker::invokeRemoteCommand(juce::CommandID commandId) {
    commandManager_.invokeDirectly(commandId, false);
}

// FRO253 (docs/control/midi-remote.md#node-command-targets): MESSAGE THREAD, called from
// RemoteEngine::drain() on a buttonPress event for a nodeCommand target. toggleSolo is the only
// command today -- this does EXACTLY what MixerColumnComponent::toggleSoloed does (same undo
// bracket, same never-strip->setSoloed()-directly rule, Source/CLAUDE.md's mixer-solo invariant)
// rather than sharing its body, since Core's RemoteActionInvoker seam can't reach a UI component.
// onNodeCommandApplied then re-syncs the mixer column's own M/S visuals -- see that field's own
// comment on why nothing else does this for a non-click solo change.
void MainComponentRemoteActionInvoker::invokeNodeCommand(juce::AudioProcessorGraph::NodeID nodeId,
                                                         synth::NodeCommandKind command) {
    if (command != synth::NodeCommandKind::toggleSolo)
        return; // the only member RemoteModel.h's NodeCommandKind defines today

    auto& graph = audioEngine_.getGraph();
    auto* node = graph.getNodeForId(nodeId);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    if (strip == nullptr)
        return; // orphaned in practice (RemoteEngineReconcile.cpp), but never trust a stale nodeId

    undoManager_.captureBeforeState(graph);
    audioEngine_.setChannelStripSoloed(nodeId, !strip->isSoloed());
    undoManager_.pushSnapshotFromCapture(graph);

    if (onNodeCommandApplied)
        onNodeCommandApplied(nodeId);
}
