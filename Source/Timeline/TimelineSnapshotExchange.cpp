#include "TimelineSnapshotExchange.h"
#include <algorithm>

namespace synth {

const TimelineSnapshot& TimelineSnapshotExchange::emptySnapshot() noexcept {
    static const TimelineSnapshot empty;
    return empty;
}

TimelineSnapshotExchange::TimelineSnapshotExchange() {
    // Force the static's initialisation here, on the constructing (message) thread: touching it
    // first from beginAudioBlock() would make the audio thread run the guarded initialisation,
    // which can allocate and, in the racing case, block.
    (void)emptySnapshot();
}

TimelineSnapshotExchange::~TimelineSnapshotExchange() { reclaimAllUnsafe(); }

const TimelineSnapshot& TimelineSnapshotExchange::beginAudioBlock() noexcept {
    // Order matters and is the whole reclamation argument: bump first, then load. seq_cst on both
    // so the bump cannot be reordered after the load and so the publisher's swap/epoch-read sit in
    // the same total order (see the header).
    audioEpoch.fetch_add(1, std::memory_order_seq_cst);
    auto* snapshot = current.load(std::memory_order_seq_cst);
    return snapshot != nullptr ? *snapshot : emptySnapshot();
}

void TimelineSnapshotExchange::publish(std::unique_ptr<TimelineSnapshot> snapshot) {
    auto* previous = current.exchange(snapshot.release(), std::memory_order_seq_cst);

    if (previous != nullptr) {
        // Tag with the epoch as observed AFTER the swap: any callback that can still be holding
        // `previous` bumped the epoch to at most this value.
        retired.push_back({std::unique_ptr<TimelineSnapshot>(previous), audioEpoch.load(std::memory_order_seq_cst)});
    }

    reap();
}

void TimelineSnapshotExchange::reap() {
    if (retired.empty())
        return;

    const auto epoch = audioEpoch.load(std::memory_order_seq_cst);
    const auto isReclaimable = [epoch](const Retired& entry) { return entry.epoch <= epoch - kEpochGrace; };

    retired.erase(std::remove_if(retired.begin(), retired.end(), isReclaimable), retired.end());
}

int TimelineSnapshotExchange::retiredCount() const { return static_cast<int>(retired.size()); }

void TimelineSnapshotExchange::reclaimAllUnsafe() {
    retired.clear();

    if (auto* previous = current.exchange(nullptr, std::memory_order_seq_cst))
        delete previous;
}

} // namespace synth
