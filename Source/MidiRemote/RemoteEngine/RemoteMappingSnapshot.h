#pragma once

// The immutable live mapping table (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads),
// published through SnapshotPublisherOf (SnapshotPublisher.h holds the proof).

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteEngine/SnapshotPublisher.h"
#include "MidiRemote/RemoteModel.h"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <juce_audio_processors/juce_audio_processors.h>
#include <memory>
#include <vector>

namespace synth::midi {

/** Packs (source, type, channel, number) into one sortable integer so the MIDI thread's lookup is a
 *  binary search over a flat array — no hashing, no allocation, no string compare. `channel` is
 *  0..16 (0 = the profile's "any channel"); `number` is 14 bits so an NRPN address fits (bits 0-13,
 *  channel 14-18, type 19-21, source 22-26). */
inline std::uint32_t packLookupKey(int sourceIndex, MessageType type, int channel, int number) noexcept {
    return (static_cast<std::uint32_t>(sourceIndex) << 22) | (static_cast<std::uint32_t>(type) << 19) |
           (static_cast<std::uint32_t>(channel) << 14) | static_cast<std::uint32_t>(number & 0x3fff);
}

/** Immutable once published. Built on the message thread by RemoteEngine::rebuildSnapshot. */
struct RemoteMappingSnapshot {
    struct SourceEntry {
        juce::String key;        // juce::MidiInput identifier, or hostSourceKey()
        int laneIndex = 0;       // index into RemoteEngine's lane pool; stable for the engine's life
        bool passMapped = false; // the owning profile's "also pass mapped messages to the patch"
    };

    /** One assignment, with its target already resolved by the last reconcile. */
    struct Slot {
        juce::String assignmentId;
        Encoding encoding = Encoding::abs7;
        ButtonMode buttonMode = ButtonMode::momentary;
        Takeover takeover = Takeover::useDefault;
        double rangeMin = 0.0;
        double rangeMax = 1.0;
        Target target;
        /** Decided at snapshot-build time so the MIDI path never has to ask what kind of target it
         *  is looking at: true for action targets, for a control whose kind is button/pad, and for
         *  a resolved juce::AudioParameterBool. Drives note/CC-127-0 decoding and buttonMode. */
        bool buttonLike = false;
        // Resolved on the message thread; read only on the message thread. The MIDI path never
        // dereferences these — it only reports the slot index.
        juce::AudioProcessorParameter* param = nullptr;
        juce::CommandID commandId = 0;
        // The resolved graph node for a nodeCommand target (Target::isNodeCommand()) --
        // resolved by nodeUuid alongside `param` above, on the message thread, in
        // RemoteEngineReconcile.cpp.
        juce::AudioProcessorGraph::NodeID nodeId;
        bool orphaned = false;
        // False for a project assignment on a page that isn't its profile's active one. Such a slot
        // is resolved but has no lookup entry, and feedback skips it too, so the controller only
        // ever echoes the page it is actually driving (see docs/control/midi-remote.md#pages).
        bool onActivePage = true;
        // target.continuous.kind, copied out here so the apply path never has to reach
        // through `target` -- masterVolume resolves `param` above through the injected
        // ContinuousParameterLookup and is otherwise identical to a parameter slot; bpm/playhead
        // have no `param` and are NEVER orphaned (there is nothing to resolve against a graph).
        ContinuousTargetKind continuous = ContinuousTargetKind::bpm;
        /** `spec.number`, readable on the MIDI path -- the MSB CC (or NRPN address) a paired
         *  slot decides "which half arrived?" against. Never changes after the snapshot is built. */
        int messageNumber = 0;
        // The assignment's own message spec and owning profile id, resolved at snapshot-build time same
        // as everything else here. MESSAGE THREAD ONLY -- the MIDI path never reads either field, only
        // RemoteEngineFeedback.cpp's drain-time echo does
        // (see docs/control/midi-remote.md#controller-feedback).
        MessageSpec spec;
        juce::String profileId;
    };

    /** Bumped on every publish, copied into every RemoteEvent the MIDI path pushes so the drain
     *  can tell a queued index that is still meaningful from one that is not. */
    std::uint32_t generation = 0;
    std::vector<SourceEntry> sources;
    std::vector<Slot> slots;
    // Parallel sorted arrays: lookupKeys[i] -> lookupSlots[i]. Sorted ascending by key.
    std::vector<std::uint32_t> lookupKeys;
    std::vector<std::int32_t> lookupSlots;

    // MIDI/AUDIO THREAD. -1 when the key is not in the table.
    int findSource(const juce::String& key) const noexcept {
        for (std::size_t i = 0; i < sources.size(); ++i)
            if (sources[i].key == key)
                return static_cast<int>(i);
        return -1;
    }

    // MIDI/AUDIO THREAD. Exact channel wins; a profile entry on channel 0 ("any") is the fallback.
    int findSlot(int sourceIndex, MessageType type, int channel, int number) const noexcept {
        const int exact = lookupExact(packLookupKey(sourceIndex, type, channel, number));
        if (exact >= 0)
            return exact;
        return lookupExact(packLookupKey(sourceIndex, type, 0, number));
    }

    bool isEmpty() const noexcept { return slots.empty(); }

private:
    int lookupExact(std::uint32_t key) const noexcept {
        const auto it = std::lower_bound(lookupKeys.begin(), lookupKeys.end(), key);
        if (it == lookupKeys.end() || *it != key)
            return -1;
        return lookupSlots[static_cast<std::size_t>(it - lookupKeys.begin())];
    }
};

// The mapping table's publisher: the one discipline in SnapshotPublisher.h.
using SnapshotPublisher = SnapshotPublisherOf<RemoteMappingSnapshot>;

} // namespace synth::midi
