#pragma once

#include "TimelineSnapshot.h"
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace synth {

// Hands TimelineSnapshots from the message thread to the audio thread (TL2-2): one atomic pointer
// for publication, an epoch counter for reclamation. No shared_ptr, no atomic<shared_ptr>, no
// locks, and no allocation or deallocation on the audio path — the whole read side is two atomic
// operations and a branch.
//
// Why not atomic<shared_ptr>: on every implementation we care about it is either lock-based or a
// double-word CAS plus a refcount RMW *per read*, and it makes the audio thread run a destructor
// (and therefore free()) whenever it happens to drop the last reference. Both are disqualifying on
// the audio path, so the audio thread never owns anything here — it only borrows.
//
// -- Contract ----------------------------------------------------------------------------------
//
// AUDIO THREAD, exactly once per callback, while holding NO pointer or reference obtained in a
// previous block:
//
//     const TimelineSnapshot& timeline = exchange.beginAudioBlock();
//
// The returned reference is valid until the next beginAudioBlock() call on this exchange and NEVER
// across blocks — caching it in a member and reading it next block is a use-after-free. It is never
// null: an exchange with nothing published yet returns emptySnapshot(), a zero-track instance that
// lives for the process's lifetime.
//
// MESSAGE THREAD:
//
//     exchange.publish(TimelineSnapshot::buildFrom(doc));   // hand over ownership
//     exchange.reap();                                      // optional, e.g. on a timer
//
// publish() swaps the new snapshot in, pushes the displaced one onto a message-thread-only retire
// list tagged with the audio epoch observed AT RETIRE TIME, then reaps everything whose tag is
// <= audioEpoch - 2.
//
// -- Why epoch - 2 is safe ----------------------------------------------------------------------
//
// beginAudioBlock() bumps the epoch BEFORE it loads the pointer, and both operations (plus the
// publisher's swap and epoch read) are seq_cst, so all four sit in one total order. Take a callback
// C whose load returned the snapshot being retired: C's load must precede the publisher's swap in
// that order (it did not see the new pointer), C's epoch bump precedes its own load, and the
// publisher's epoch read follows its swap. So the epoch value R the publisher tags the retiree with
// is at least the value C's own bump produced. Once the epoch reaches R + 2, at least two further
// callbacks have STARTED — and starting one is, by the contract above, a promise that the previous
// block's reference has been dropped. So nobody can still be reading the retiree, and freeing it on
// the message thread is safe. (R + 1 would already do under a single audio thread; the extra epoch
// is free and removes any need to reason about that assumption.)
//
// Memory ordering note: the spec for this class called for a release bump plus an acquire load.
// That is not enough on its own — release does not stop the following load from being hoisted above
// the bump, and nothing weaker than seq_cst gives the two threads the single total order the
// argument above uses. Since this runs once per audio callback (a few thousand times a second at
// worst) the cost is noise, exactly like TransportService's seq_cst position snapshot, and the
// correctness argument stays one paragraph long.
//
// -- Idle audio ---------------------------------------------------------------------------------
//
// If the audio device is stopped the epoch never advances, so nothing can be reclaimed and retirees
// accumulate — bounded by the number of publishes made while idle, freed by the first reap() after
// audio restarts, or by reclaimAllUnsafe() at shutdown. While audio IS running, publish()'s own
// reap keeps the list from growing without bound (each publish frees everything retired two epochs
// ago), so steady-state occupancy is a handful of snapshots.
class TimelineSnapshotExchange {
public:
    TimelineSnapshotExchange();
    ~TimelineSnapshotExchange();

    TimelineSnapshotExchange(const TimelineSnapshotExchange&) = delete;
    TimelineSnapshotExchange& operator=(const TimelineSnapshotExchange&) = delete;

    // -- Audio thread ---------------------------------------------------------
    // Opens a block: advances the epoch, then returns the currently published snapshot. Never null,
    // never allocates, never blocks. Call it once per callback and do not hold the result past the
    // end of that callback.
    const TimelineSnapshot& beginAudioBlock() noexcept;

    // -- Message thread -------------------------------------------------------
    // Takes ownership of `snapshot` (which may be null — that republishes "nothing", and the audio
    // thread falls back to emptySnapshot()), retires the previous one and reaps what is safe.
    void publish(std::unique_ptr<TimelineSnapshot> snapshot);

    // Frees every retiree the epoch rule now allows. publish() calls this itself; it is public so a
    // host that publishes rarely can still drain the list on a timer.
    void reap();

    // Retirees still waiting for the epoch to advance. Diagnostics and tests.
    int retiredCount() const;

    // Frees EVERYTHING, including the currently published snapshot, ignoring the epoch rule.
    // Legal only when the audio thread is stopped and cannot call beginAudioBlock() — i.e. after
    // the device callback has been removed / releaseResources() has run. Calling it while audio is
    // live is a use-after-free. The destructor calls it, which is why an exchange must outlive the
    // audio callback that reads it.
    void reclaimAllUnsafe();

    // The zero-track snapshot handed out before anything is published. Constructed eagerly by the
    // first exchange so the audio thread never runs a function-local static's guarded
    // initialisation.
    static const TimelineSnapshot& emptySnapshot() noexcept;

    // Diagnostics: blocks opened so far. Not part of the contract.
    std::int64_t getAudioEpoch() const noexcept { return audioEpoch.load(std::memory_order_relaxed); }

private:
    struct Retired {
        std::unique_ptr<TimelineSnapshot> snapshot;
        std::int64_t epoch = 0; // audio epoch observed when this snapshot was displaced
    };

    // How far the epoch must advance past a retiree's tag before it can be freed. See the argument
    // above; 2 rather than 1 buys margin without costing anything.
    static constexpr std::int64_t kEpochGrace = 2;

    std::atomic<TimelineSnapshot*> current{nullptr};
    std::atomic<std::int64_t> audioEpoch{0};
    std::vector<Retired> retired; // message thread only — never touched by the audio thread
};

} // namespace synth
