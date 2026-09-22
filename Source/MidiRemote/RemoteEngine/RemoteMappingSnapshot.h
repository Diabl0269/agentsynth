#pragma once

// The immutable live mapping table and the discipline that publishes it
// (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads).
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
// at most kMaxRemoteSources readers.)
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
 *  0..16 (0 = the profile's "any channel"). */
inline std::uint32_t packLookupKey(int sourceIndex, MessageType type, int channel, int number) noexcept {
    return (static_cast<std::uint32_t>(sourceIndex) << 16) | (static_cast<std::uint32_t>(type) << 13) |
           (static_cast<std::uint32_t>(channel) << 8) | static_cast<std::uint32_t>(number & 0xff);
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
        // FRO253: the resolved graph node for a nodeCommand target (Target::isNodeCommand()) --
        // resolved by nodeUuid alongside `param` above, on the message thread, in
        // RemoteEngineReconcile.cpp.
        juce::AudioProcessorGraph::NodeID nodeId;
        bool orphaned = false;
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

/** Publishes RemoteMappingSnapshots to N lock-free readers. See the file header for the proof. */
class SnapshotPublisher {
public:
    SnapshotPublisher() = default;
    SnapshotPublisher(const SnapshotPublisher&) = delete;
    SnapshotPublisher& operator=(const SnapshotPublisher&) = delete;

    /** Destroying the publisher frees the live and every retired snapshot unconditionally. Legal
     *  only once no reader can run again — the app layer clears AudioEngine's sink before the
     *  engine is destroyed (see RemoteEngine's class comment). */
    ~SnapshotPublisher() {
        delete current.load(std::memory_order_seq_cst);
        retired.clear();
    }

    /** MIDI/AUDIO THREAD. Scoped read of the live snapshot; may yield nullptr before the first
     *  publish. Lock-free and allocation-free. */
    class ReadGuard {
    public:
        explicit ReadGuard(const SnapshotPublisher& owner) noexcept
            : publisher(owner) {
            publisher.readersInFlight.fetch_add(1, std::memory_order_seq_cst);
            snapshot = publisher.current.load(std::memory_order_seq_cst);
        }
        ~ReadGuard() noexcept { publisher.readersInFlight.fetch_sub(1, std::memory_order_seq_cst); }
        ReadGuard(const ReadGuard&) = delete;
        ReadGuard& operator=(const ReadGuard&) = delete;

        const RemoteMappingSnapshot* get() const noexcept { return snapshot; }

    private:
        const SnapshotPublisher& publisher;
        const RemoteMappingSnapshot* snapshot = nullptr;
    };

    // MESSAGE THREAD.
    void publish(std::unique_ptr<const RemoteMappingSnapshot> fresh) {
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
    const RemoteMappingSnapshot* live() const noexcept { return current.load(std::memory_order_seq_cst); }

    int retiredCount() const noexcept { return static_cast<int>(retired.size()); }

private:
    std::atomic<const RemoteMappingSnapshot*> current{nullptr};
    mutable std::atomic<int> readersInFlight{0};
    std::vector<std::unique_ptr<const RemoteMappingSnapshot>> retired; // message thread only

    static_assert(std::atomic<const RemoteMappingSnapshot*>::is_always_lock_free,
                  "the MIDI path may not take a lock to read the mapping table");
    static_assert(std::atomic<int>::is_always_lock_free, "reader counting must be lock-free");
};

} // namespace synth::midi
