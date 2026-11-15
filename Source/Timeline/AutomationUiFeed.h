#pragma once

#include "AudioEngine/SpscRing.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <utility>

namespace synth {

// One automation-driven parameter write, carried from the audio thread (AutomationApplier)
// to the message thread (GraphEditor's drain) purely so the UI can reflect it. POD and small on
// purpose — this rides a lock-free ring at up to one entry per bound parameter per block.
//
// `param` is a borrowed pointer, valid for the same reason AutomationBindingTable::Binding's is:
// the binding table holds a refcounted Node::Ptr for as long as this event could still be in the
// ring, and the ring is drained well within that window (every 1/30 s against a table that is only
// swapped, never torn down under a live event). `nodeId` is carried alongside — rather than made
// the reader's job to re-derive from `param`'s owning node — so the drain can reject a stale event
// (component deleted, node gone) with a single map lookup and without dereferencing `param` at all.
struct AutomationUiEvent {
    juce::uint32 nodeId = 0;
    const juce::AudioProcessorParameter* param = nullptr;
    float newNormalized = 0.0f;
};

// Lock-free SPSC ring of AutomationUiEvent. AutomationApplier::applyBlock (audio thread)
// pushes; GraphEditor's 30 Hz drain (message thread) reads. Pre-allocated at construction — push()
// never allocates and never blocks; a full ring drops the newest event silently, because UI
// reflection is best-effort and must never be allowed to add latency or a lock to the audio thread.
class AutomationUiFeed {
public:
    static constexpr int kCapacity = 4096;

    // AUDIO THREAD. No allocation, no locks, no logging. Silently drops `event` if the ring is full
    // — the next changed value will get through on a later block.
    void push(const AutomationUiEvent& event) noexcept { ring.push(event); }

    // MESSAGE THREAD. Invokes `fn(const AutomationUiEvent&)` once per queued event, oldest first,
    // then leaves the ring empty. Safe (and cheap) to call when nothing is queued.
    template <typename Fn>
    void drain(Fn&& fn) {
        ring.drain(std::forward<Fn>(fn));
    }

private:
    // SpscRing, not juce::AbstractFifo: the latter is not ThreadSanitizer-clean.
    SpscRing<AutomationUiEvent> ring{kCapacity};
};

} // namespace synth
