#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace synth {

// Hands an immutable, message-thread-built object to the audio thread: one atomic pointer for
// publication, an epoch counter for reclamation. No shared_ptr, no
// atomic<shared_ptr>, no locks, and no allocation or deallocation on the audio path — the whole
// read side is two atomic operations and a branch.
//
// `T` must be default-constructible: the "nothing published yet" fallback is a single static
// default-constructed instance (see emptyValue()). Nothing else is required of it — it is never
// copied, never moved and never touched by this class beyond `new`/`delete` of caller-supplied
// unique_ptrs.
//
// Two instantiations exist today: EpochExchange<TimelineSnapshot> (aliased as
// TimelineSnapshotExchange — the timeline the audio thread reads) and
// EpochExchange<AutomationBindingTable> (the resolved lane -> parameter bindings the
// automation applier walks).
//
// Why not atomic<shared_ptr>: on every implementation we care about it is either lock-based or a
// double-word CAS plus a refcount RMW *per read*, and it makes the audio thread run a destructor
// (and therefore free()) whenever it happens to drop the last reference. Both are disqualifying on
// the audio path, so the audio thread never owns anything here — it only borrows.
//
// -- Contract ----------------------------------------------------------------------------------
//
// AUDIO THREAD, exactly once per graph render pass, while holding NO pointer or reference obtained
// in a previous pass:
//
//     const T& value = exchange.beginAudioBlock();
//
// The returned reference is valid until the next beginAudioBlock() call on this exchange and NEVER
// across passes — caching it in a member and reading it next pass is a use-after-free. It is never
// null: an exchange with nothing published yet returns emptyValue(), a default-constructed
// instance that lives for the process's lifetime.
//
// "Once per render pass" rather than "once per callback": with AudioEngine's control-rate slicing
// enabled (off by default) one device callback runs the whole per-block sequence — transport
// tick, snapshot open, automation apply, graph render — once per 64-sample slice, so a callback
// opens several blocks on this exchange. That is still correct: the reclamation argument below only
// needs "starting a block is a promise the previous block's reference was dropped", which holds per
// slice exactly as it holds per callback. It makes the epoch advance FASTER, never slower.
//
// MESSAGE THREAD:
//
//     exchange.publish(std::make_unique<T>(...));   // hand over ownership
//     exchange.reap();                              // optional, e.g. on a timer
//
// publish() swaps the new value in, pushes the displaced one onto a message-thread-only retire
// list tagged with the audio epoch observed AT RETIRE TIME, then reaps everything whose tag is
// <= audioEpoch - 2.
//
// -- Why epoch - 2 is safe ----------------------------------------------------------------------
//
// beginAudioBlock() bumps the epoch BEFORE it loads the pointer, and both operations (plus the
// publisher's swap and epoch read) are seq_cst, so all four sit in one total order. Take a block B
// whose load returned the value being retired: B's load must precede the publisher's swap in that
// order (it did not see the new pointer), B's epoch bump precedes its own load, and the publisher's
// epoch read follows its swap. So the epoch value R the publisher tags the retiree with is at least
// the value B's own bump produced. Once the epoch reaches R + 2, at least two further blocks have
// STARTED — and starting one is, by the contract above, a promise that the previous block's
// reference has been dropped. So nobody can still be reading the retiree, and freeing it on the
// message thread is safe. (R + 1 would already do under a single audio thread; the extra epoch is
// free and removes any need to reason about that assumption.)
//
// Memory ordering note: the spec for this class called for a release bump plus an acquire load.
// That is not enough on its own — release does not stop the following load from being hoisted above
// the bump, and nothing weaker than seq_cst gives the two threads the single total order the
// argument above uses. Since this runs once per render pass (a few thousand times a second at
// worst) the cost is noise, exactly like TransportService's seq_cst position snapshot, and the
// correctness argument stays one paragraph long.
//
// -- Idle audio ---------------------------------------------------------------------------------
//
// If the audio device is stopped the epoch never advances, so nothing can be reclaimed and retirees
// accumulate — bounded by the number of publishes made while idle, freed by the first reap() after
// audio restarts, or by reclaimAllUnsafe() at shutdown. While audio IS running, publish()'s own
// reap keeps the list from growing without bound (each publish frees everything retired two epochs
// ago), so steady-state occupancy is a handful of instances.
template <typename T>
class EpochExchange {
public:
    EpochExchange() {
        // Force the static's initialisation here, on the constructing (message) thread: touching it
        // first from beginAudioBlock() would make the audio thread run the guarded initialisation,
        // which can allocate and, in the racing case, block.
        (void)emptyValue();
    }

    ~EpochExchange() { reclaimAllUnsafe(); }

    EpochExchange(const EpochExchange&) = delete;
    EpochExchange& operator=(const EpochExchange&) = delete;

    // -- Audio thread ---------------------------------------------------------
    // Opens a block: advances the epoch, then returns the currently published value. Never null,
    // never allocates, never blocks. Call it once per render pass and do not hold the result past
    // the end of that pass.
    const T& beginAudioBlock() noexcept {
        // Order matters and is the whole reclamation argument: bump first, then load. seq_cst on
        // both so the bump cannot be reordered after the load and so the publisher's swap/epoch-read
        // sit in the same total order (see above).
        audioEpoch.fetch_add(1, std::memory_order_seq_cst);
        auto* value = current.load(std::memory_order_seq_cst);
        return value != nullptr ? *value : emptyValue();
    }

    // -- Message thread -------------------------------------------------------
    // Takes ownership of `value` (which may be null — that republishes "nothing", and the audio
    // thread falls back to emptyValue()), retires the previous one and reaps what is safe.
    void publish(std::unique_ptr<T> value) {
        auto* previous = current.exchange(value.release(), std::memory_order_seq_cst);

        if (previous != nullptr) {
            // Tag with the epoch as observed AFTER the swap: any block that can still be holding
            // `previous` bumped the epoch to at most this value.
            retired.push_back({std::unique_ptr<T>(previous), audioEpoch.load(std::memory_order_seq_cst)});
        }

        reap();
    }

    // Frees every retiree the epoch rule now allows. publish() calls this itself; it is public so a
    // host that publishes rarely can still drain the list on a timer.
    void reap() {
        if (retired.empty())
            return;

        const auto epoch = audioEpoch.load(std::memory_order_seq_cst);
        const auto isReclaimable = [epoch](const Retired& entry) { return entry.epoch <= epoch - kEpochGrace; };

        retired.erase(std::remove_if(retired.begin(), retired.end(), isReclaimable), retired.end());
    }

    // Retirees still waiting for the epoch to advance. Diagnostics and tests.
    int retiredCount() const { return static_cast<int>(retired.size()); }

    // Frees EVERYTHING, including the currently published value, ignoring the epoch rule.
    // Legal only when the audio thread is stopped and cannot call beginAudioBlock() — i.e. after
    // the device callback has been removed / releaseResources() has run. Calling it while audio is
    // live is a use-after-free. The destructor calls it, which is why an exchange must outlive the
    // audio callback that reads it.
    void reclaimAllUnsafe() {
        retired.clear();

        if (auto* previous = current.exchange(nullptr, std::memory_order_seq_cst))
            delete previous;
    }

    // The default-constructed instance handed out before anything is published. Constructed eagerly
    // by the first exchange of this T so the audio thread never runs a function-local static's
    // guarded initialisation.
    //
    // Deliberately a non-const static returned by const reference: a published T may carry `mutable`
    // audio-thread scratch (AutomationBindingTable's per-binding cursors), and writing through a
    // mutable member of a genuinely const object is UB. Nothing ever writes to this instance today —
    // the empty binding table has no bindings to iterate — but declaring it const would make that a
    // latent trap rather than a caught one.
    static const T& emptyValue() noexcept {
        static T empty;
        return empty;
    }

    // Diagnostics: blocks opened so far. Not part of the contract.
    std::int64_t getAudioEpoch() const noexcept { return audioEpoch.load(std::memory_order_relaxed); }

private:
    struct Retired {
        std::unique_ptr<T> value;
        std::int64_t epoch = 0; // audio epoch observed when this value was displaced
    };

    // How far the epoch must advance past a retiree's tag before it can be freed. See the argument
    // above; 2 rather than 1 buys margin without costing anything.
    static constexpr std::int64_t kEpochGrace = 2;

    std::atomic<T*> current{nullptr};
    std::atomic<std::int64_t> audioEpoch{0};
    std::vector<Retired> retired; // message thread only — never touched by the audio thread
};

} // namespace synth
