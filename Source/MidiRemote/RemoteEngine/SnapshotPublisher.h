#pragma once

// The one publish discipline every MIDI-path table uses
// (docs/control/midi-remote.md#threading-the-mapping-table-crosses-threads): RemoteEngine's mapping table
// (RemoteMappingSnapshot) and RemoteSinkRouter's owned-source table are both an SnapshotPublisherOf<T>, never a second
// mechanism.
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

#include <atomic>
#include <memory>
#include <vector>

namespace synth::midi {

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

} // namespace synth::midi
