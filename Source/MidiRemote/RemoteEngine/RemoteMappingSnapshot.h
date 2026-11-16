#pragma once

// The immutable live mapping table and the discipline that publishes it
// (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads). RemoteEngine's mapping table and
// RemoteSinkRouter's owned-source table are both a SnapshotPublisherOf<T>, never a second mechanism.
//
// WHY NEITHER OF THE TWO OBVIOUS ANSWERS.
//
//  * synth::EpochExchange (Source/Timeline/EpochExchange.h) is single-consumer by construction: one
//    epoch counter, bumped once per audio block by the one audio thread. The MIDI path has no block
//    boundary to bump on, and standalone can have several device threads reading at once. Its grace
//    rule ("retire is safe two epochs later") simply does not have a meaning here.
//
//  * std::atomic<std::shared_ptr<const Snapshot>> is not lock-free in any shipping standard
//    library: libstdc++ and MSVC use a spinlock, libc++ a mutex pool. In HostMode::Hosted this code
//    runs on the *audio* thread, so that is a lock on the audio thread contending with the message
//    thread — precisely the tripwire (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads) exists
//    to prevent.
//
// SO: an atomic raw pointer plus a reader count plus a message-thread retire list.
//
//   reader (any thread):   readersInFlight.fetch_add(1); p = current.load(); ...use p...;
//                          readersInFlight.fetch_sub(1);
//   publisher (msg thread): old = current.exchange(fresh); retired.push_back(old);
//   collector (msg thread): if (readersInFlight.load() == 0) retired.clear();
//
// WHY THAT IS SAFE, under seq_cst (one total order over all these operations). Suppose a reader is
// dereferencing a snapshot S that sits in `retired`. That reader loaded current == S, so its load
// precedes the exchange that replaced S, and its fetch_add precedes its own load. Therefore
// fetch_add < exchange < the collector's check. If the check reads 0, this reader's fetch_sub must
// already have happened — otherwise the count would still have counted it — so the reader is done
// with S before the free. A reader whose fetch_add happens *after* the check cannot obtain S at
// all, because S was exchanged out before the check. Hence nothing in `retired` can be in use when
// the count reads zero. (Hazard pointers give the same guarantee for more code than is needed with
// at most kMaxRemoteSources readers.) Anything a snapshot OWNS (the router's surfaces) is freed with
// it, so it is covered by the same argument for as long as a reader holds the guard.
//
// The collector is called from the same 60 Hz drain that reads the FIFOs, so a busy controller
// simply defers a free by a frame or two; nothing spins and nothing waits.

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
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

/** The most distinct (address, argIndex) keys one source interns: the packed key's `number` field is 14 bits. */
inline constexpr int kMaxOscKeysPerSource = 1 << 14;

/** Immutable once published. Built on the message thread by RemoteEngine::rebuildSnapshot. */
struct RemoteMappingSnapshot {
    /** One interned OSC (address, argIndex). A source's keys are sorted by (address, argIndex) and a key's
     *  position is its dense id, the `number` of its packed lookup key. */
    struct OscKey {
        juce::String address;
        int argIndex = 0;
    };

    struct SourceEntry {
        juce::String key;        // juce::MidiInput identifier, or hostSourceKey()
        int laneIndex = 0;       // index into RemoteEngine's lane pool; stable for the engine's life
        bool passMapped = false; // the owning profile's "also pass mapped messages to the patch"
        /** Bit (c - 1) set = channel c is an MPE member channel of the owning profile's FORCED zone
         *  layout (0 for automatic/off). findSlot never lets the "any channel" fallback claim
         *  per-note expression on such a channel. */
        std::uint16_t forcedMemberMask = 0;
        std::vector<OscKey> oscKeys; // sorted by (address, argIndex); empty for a MIDI-only source

        /** NETWORK/MIDI THREAD. The dense id of (address, argIndex), or -1. Binary search with
         *  juce::String::compare: compares, never constructs, never allocates. */
        int findOscId(const juce::String& address, int argIndex) const noexcept {
            std::size_t lo = 0;
            std::size_t hi = oscKeys.size();
            while (lo < hi) {
                const std::size_t mid = lo + (hi - lo) / 2;
                const int order = oscKeys[mid].address.compare(address);
                const int cmp =
                    order != 0 ? order
                               : (oscKeys[mid].argIndex < argIndex ? -1 : (oscKeys[mid].argIndex > argIndex ? 1 : 0));
                if (cmp == 0)
                    return static_cast<int>(mid);
                if (cmp < 0)
                    lo = mid + 1;
                else
                    hi = mid;
            }
            return -1;
        }
    };

    /** One assignment, with its target already resolved by the last reconcile. */
    struct Slot {
        juce::String assignmentId;
        Encoding encoding = Encoding::abs7;
        ButtonMode buttonMode = ButtonMode::momentary;
        Takeover takeover = Takeover::useDefault;
        double rangeMin = 0.0;
        double rangeMax = 1.0;
        /** An osc control's input range (Control::inRange), copied at snapshot-build time. */
        double inMin = 0.0;
        double inMax = 1.0;
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

    // MIDI/AUDIO THREAD. Exact channel wins; a profile entry on channel 0 ("any") is the fallback,
    // except for per-note expression (pitch bend, channel pressure, CC 74, notes) on a member
    // channel of the source's forced MPE zone: that belongs to the synth, so it never falls back.
    int findSlot(int sourceIndex, MessageType type, int channel, int number) const noexcept {
        const int exact = lookupExact(packLookupKey(sourceIndex, type, channel, number));
        if (exact >= 0)
            return exact;
        if (isMemberChannelExpression(sourceIndex, type, channel, number))
            return -1;
        return lookupExact(packLookupKey(sourceIndex, type, 0, number));
    }

    bool isEmpty() const noexcept { return slots.empty(); }

private:
    static constexpr int kMpeTimbreCc = 74;

    bool isMemberChannelExpression(int sourceIndex, MessageType type, int channel, int number) const noexcept {
        if (channel < 1 || channel > 16 || static_cast<std::size_t>(sourceIndex) >= sources.size())
            return false;
        if ((sources[static_cast<std::size_t>(sourceIndex)].forcedMemberMask & (1u << (channel - 1))) == 0)
            return false;
        return type == MessageType::pitchBend || type == MessageType::channelPressure || type == MessageType::note ||
               (type == MessageType::cc && number == kMpeTimbreCc);
    }

    int lookupExact(std::uint32_t key) const noexcept {
        const auto it = std::lower_bound(lookupKeys.begin(), lookupKeys.end(), key);
        if (it == lookupKeys.end() || *it != key)
            return -1;
        return lookupSlots[static_cast<std::size_t>(it - lookupKeys.begin())];
    }
};

/** Publishes immutable `Snapshot`s to N lock-free readers. See the file header for the proof. */
template <typename Snapshot>
class SnapshotPublisherOf {
public:
    SnapshotPublisherOf() = default;
    SnapshotPublisherOf(const SnapshotPublisherOf&) = delete;
    SnapshotPublisherOf& operator=(const SnapshotPublisherOf&) = delete;

    /** Destroying the publisher frees the live and every retired snapshot unconditionally. Legal
     *  only once no reader can run again — the app layer clears AudioEngine's sink before the
     *  engine is destroyed (see RemoteEngine's class comment). */
    ~SnapshotPublisherOf() {
        delete current.load(std::memory_order_seq_cst);
        retired.clear();
    }

    /** MIDI/AUDIO THREAD. Scoped read of the live snapshot; may yield nullptr before the first
     *  publish. Lock-free and allocation-free. */
    class ReadGuard {
    public:
        explicit ReadGuard(const SnapshotPublisherOf& owner) noexcept
            : publisher(owner) {
            publisher.readersInFlight.fetch_add(1, std::memory_order_seq_cst);
            snapshot = publisher.current.load(std::memory_order_seq_cst);
        }
        ~ReadGuard() noexcept { publisher.readersInFlight.fetch_sub(1, std::memory_order_seq_cst); }
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;

        const Snapshot* get() const noexcept { return snapshot; }

    private:
        const SnapshotPublisherOf& publisher;
        const Snapshot* snapshot = nullptr;
    };

    // MESSAGE THREAD.
    void publish(std::unique_ptr<const Snapshot> fresh) {
        const auto* old = current.exchange(fresh.release(), std::memory_order_seq_cst);
        if (old != nullptr)
            retired.emplace_back(old);
    }

    // MESSAGE THREAD. Called from the drain tick; frees whatever no reader can still hold.
    void collectRetired() noexcept {
        if (retired.empty())
            return;
        if (readersInFlight.load(std::memory_order_seq_cst) == 0)
            retired.clear();
    }

    // MESSAGE THREAD. The published table, for building the next one from. May be nullptr.
    const Snapshot* live() const noexcept { return current.load(std::memory_order_seq_cst); }

    int retiredCount() const noexcept { return static_cast<int>(retired.size()); }

private:
    std::atomic<const Snapshot*> current{nullptr};
    mutable std::atomic<int> readersInFlight{0};
    std::vector<std::unique_ptr<const Snapshot>> retired; // message thread only

    static_assert(std::atomic<const Snapshot*>::is_always_lock_free,
                  "the MIDI path may not take a lock to read a published table");
    static_assert(std::atomic<int>::is_always_lock_free, "reader counting must be lock-free");
};

// The mapping table's publisher.
using SnapshotPublisher = SnapshotPublisherOf<RemoteMappingSnapshot>;

} // namespace synth::midi
