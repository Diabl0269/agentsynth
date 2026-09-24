// See MainComponentRemoteActionInvoker.h for what this is and why it's a standalone class.

#include "MainComponentRemoteActionInvoker.h"

#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ChannelStripModule.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>

MainComponentRemoteActionInvoker::MainComponentRemoteActionInvoker(juce::ApplicationCommandManager& cm,
                                                                   AudioEngine& engine, AppUndoManager& undo,
                                                                   synth::TransportNudgeState& transportNudge,
                                                                   const synth::TimelineDoc& timelineDoc) noexcept
    : commandManager_(cm)
    , audioEngine_(engine)
    , undoManager_(undo)
    , transportNudge_(transportNudge)
    , timelineDoc_(timelineDoc) {}

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

// FRO236 (docs/control/midi-remote.md#continuous-targets): MESSAGE THREAD, called from
// RemoteEngine::drain() to read a continuous target's current value in native units.
double MainComponentRemoteActionInvoker::getContinuousValue(synth::ContinuousTargetKind kind) {
    const auto snap = audioEngine_.getTransport().getPositionSnapshot();
    if (kind == synth::ContinuousTargetKind::bpm)
        return snap.bpm;

    // playhead: a relative move must accumulate several fast detents inside one audio block exactly
    // like FRO271's cursor-move actions do (TransportNudge.h's own comment on why
    // TransportNudgeState exists) -- return the still-pending request's own target while it is
    // unconsumed (the audio thread hasn't applied it yet), not the stale snapshot. This is the same
    // "unconsumed" check computeNudgeTarget() makes; setContinuousValue below finishes the
    // equivalence by calling locateTransportTracked(), so the pair together behave exactly like
    // nudgeTransportCursor() would.
    const bool unconsumed =
        transportNudge_.pending && transportNudge_.baseSample == snap.samplePosition &&
        transportNudge_.basePpq == snap.ppq &&
        static_cast<std::uint32_t>(juce::Time::getMillisecondCounter() - transportNudge_.requestedAtMs) <=
            synth::kNudgeAccumulateWindowMs;
    return unconsumed ? transportNudge_.target : snap.ppq;
}

// FRO236: MESSAGE THREAD. A no-op in the plugin build -- the host owns the transport
// (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host).
void MainComponentRemoteActionInvoker::setContinuousValue(synth::ContinuousTargetKind kind, double native) {
    if (audioEngine_.isHosted())
        return;
    auto& transport = audioEngine_.getTransport();
    if (kind == synth::ContinuousTargetKind::bpm) {
        transport.setBpm(juce::jlimit(synth::TransportService::kMinBpm, synth::TransportService::kMaxBpm, native));
        return;
    }
    // playhead (bpm handled above; masterVolume never reaches this invoker at all -- it goes
    // through the parameter path). locateTransportTracked() records transportNudge_ the same way
    // nudgeTransportCursor() would; getContinuousValue() above is what makes the pair equivalent for
    // a relative move.
    synth::locateTransportTracked(transport, transportNudge_, native);
}

// FRO236: MESSAGE THREAD. bpm never calls this (its window is Core's own
// kRemoteBpmWindowMin/Max); masterVolume never calls this either (it has no window -- it resolves
// through the parameter path's own [0,1] range). False in the plugin build (no transport to own).
bool MainComponentRemoteActionInvoker::getContinuousWindow(synth::ContinuousTargetKind kind, double& lo, double& hi) {
    if (kind != synth::ContinuousTargetKind::playhead || audioEngine_.isHosted())
        return false;

    const auto snap = audioEngine_.getTransport().getPositionSnapshot();
    if (snap.looping && snap.loopEndPpq > snap.loopStartPpq) {
        lo = snap.loopStartPpq;
        hi = snap.loopEndPpq;
        return true;
    }

    // No loop: 0..the arrangement end, rounded UP to a whole bar, minimum 8 bars
    // (docs/control/midi-remote.md#continuous-targets).
    const double beatsPerBar = synth::beatsPerBarOf(snap);
    const double barsNeeded = std::max(8.0, std::ceil(timelineDoc_.getArrangementEndBeat() / beatsPerBar));
    lo = 0.0;
    hi = barsNeeded * beatsPerBar;
    return true;
}
