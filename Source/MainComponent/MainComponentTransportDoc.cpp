// MainComponentTransportDoc.cpp — MainComponent's handling of the transport state a project persists
// (tempo, time signature, loop): the debounced poll that turns transport edits into undo steps, and
// applying a just-loaded bundle's values. MainComponent is declared in MainComponent.h; the rest of its
// implementation lives in the sibling MainComponent*.cpp units next to this one.
#include "AudioEngine/AudioEngine.h"
#include "MainComponent.h"

namespace {
// A tempo or loop drag changes the value every tick; one undo step is recorded only once the value has
// stopped moving for this long.
constexpr juce::uint32 kTransportEditDebounceMs = 300;
} // namespace

// The transport is edited from several places (tempo field, tap tempo, time signature, ruler loop drag,
// MIDI Remote) that never touch the undo manager, so the edits are found by comparing the requested state
// against the last recorded one on the timer tick. The requested state (getDocumentState), not the audio
// snapshot, is what is compared: the snapshot lags a tick and never moves without an audio device. Skipped
// during a bounce, which sets the loop temporarily and restores it, so that is never an edit.
void MainComponent::pollTransportEdits(juce::uint32 nowMs) {
    if (isBounceInProgress_)
        return;

    auto& transport = audioEngine.getTransport();
    const synth::TransportDoc cur = transport.getDocumentState();
    if (cur == committedTransport_) {
        pendingTransport_ = committedTransport_;
        return;
    }
    if (cur != pendingTransport_) {
        pendingTransport_ = cur;
        pendingTransportSinceMs_ = nowMs;
        return;
    }
    if (nowMs - pendingTransportSinceMs_ < kTransportEditDebounceMs)
        return;

    // The lambda is what undo/redo run: it applies the state and moves the baseline with it, so the
    // restore is not seen by the next poll as a fresh edit.
    undoManager.recordTransportChange(
        [this](const synth::TransportDoc& doc) {
            audioEngine.getTransport().applyDocumentState(doc);
            committedTransport_ = doc;
            pendingTransport_ = doc;
        },
        committedTransport_, cur);
    committedTransport_ = cur;
}

// Opening a project applies its transport and rebases the baseline in the same step, so the load itself
// is never seen as an edit (it must leave the document clean).
void MainComponent::applyLoadedTransport(const synth::TransportDoc& loaded) {
    audioEngine.getTransport().applyDocumentState(loaded);
    committedTransport_ = loaded;
    pendingTransport_ = loaded;
}
