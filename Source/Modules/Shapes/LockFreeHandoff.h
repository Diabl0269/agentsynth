// LockFreeHandoff.h -- the lock-free, allocation-free way a message thread gives an audio thread a new immutable
// table. Shared by the Filter's Custom engine and the LFO's shape blender.
#pragma once

#include <array>
#include <atomic>
#include <juce_core/juce_core.h>
#include <memory>

namespace synth {

/** The handoff is a single pending slot plus a retire FIFO, so the audio thread never allocates or frees a table:
    - The message thread renders a table and EXCHANGES it into `pending_` (publish()). If it gets a table back,
      the audio thread never took that one, so it is freed right here, safely: the exchange is the
      only way the audio thread could also have claimed it.
    - The audio thread exchanges `pending_` with null at the top of a block (adopt()); on a hit it pushes its
      old `current_` into the retire FIFO and adopts the new one.
    - The message thread frees whatever is in the retire FIFO on every publish (and the destructor
      on teardown). The FIFO only fills if the audio thread adopts more than kRetireCapacity tables
      between two publishes, which cannot happen (one adoption per publish); if it ever did, the old
      table would be leaked rather than freed on the audio thread. */
template <class Table>
class LockFreeHandoff {
public:
    LockFreeHandoff() = default;
    LockFreeHandoff(const LockFreeHandoff&) = delete;
    LockFreeHandoff& operator=(const LockFreeHandoff&) = delete;

    /** Runs once the audio thread can no longer call adopt() (the owner is being destroyed), so every table --
        pending, current and retired -- is freed here. */
    ~LockFreeHandoff() {
        delete pending_.exchange(nullptr);
        delete current_;
        freeRetired();
    }

    /** Message thread only. */
    void publish(std::unique_ptr<Table> table) {
        freeRetired();
        delete pending_.exchange(table.release(), std::memory_order_acq_rel);
    }

    /** Audio thread only. True when a newly published table became current. */
    bool adopt() noexcept {
        Table* incoming = pending_.exchange(nullptr, std::memory_order_acq_rel);
        if (incoming == nullptr)
            return false;
        if (current_ != nullptr) {
            const auto scope = retireFifo_.write(1);
            if (scope.blockSize1 > 0)
                retired_[(size_t)scope.startIndex1] = current_;
        }
        current_ = incoming;
        return true;
    }

    /** Audio thread only: the adopted table, or null before the first adoption. */
    const Table* current() const noexcept { return current_; }

private:
    void freeRetired() noexcept {
        const auto scope = retireFifo_.read(retireFifo_.getNumReady());
        for (int i = 0; i < scope.blockSize1; ++i)
            delete retired_[(size_t)(scope.startIndex1 + i)];
        for (int i = 0; i < scope.blockSize2; ++i)
            delete retired_[(size_t)(scope.startIndex2 + i)];
    }

    static constexpr int kRetireCapacity = 32;
    std::atomic<Table*> pending_{nullptr};
    Table* current_ = nullptr; // audio thread only
    std::array<Table*, kRetireCapacity> retired_{};
    juce::AbstractFifo retireFifo_{kRetireCapacity};
};

} // namespace synth
