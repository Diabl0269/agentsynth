#pragma once

// Lock-free single-producer / single-consumer ring for trivially copyable payloads crossing a
// thread boundary (audio or MIDI thread -> message thread).
//
// WHY THIS IS NOT juce::AbstractFifo. AbstractFifo publishes its indices through juce::Atomic, whose
// get()/set() are seq_cst, but it gives ThreadSanitizer no release/acquire pairing between the
// *payload* write and the index publication, so a one-producer / one-consumer handoff over it is
// reported as a data race on the slot (reproduced against the real AutomationUiFeed).
// This ring publishes the payload explicitly: the producer stores the element, then releases the
// write index; the consumer acquires the write index, then reads the element, and releases the read
// index only after it has finished with every slot it read; the producer acquires the read index
// before it may overwrite a slot, so a slot in flight is never reclaimed. One slot is always left
// empty, which is what makes full and empty distinct. Pre-allocated at construction: push() never
// allocates, blocks or logs, and a full ring drops the NEWEST element.
//
// Several sequential producers on different threads are fine as long as they never overlap (the
// release write index orders a later producer after the earlier one); two concurrent producers are
// not. Tests/AudioEngine/SpscRingTests.cpp is the TSan-run contract test.

#include <atomic>
#include <cstddef>
#include <type_traits>
#include <vector>

namespace synth {

template <typename T>
class SpscRing {
    static_assert(std::is_trivially_copyable_v<T>, "an SPSC ring slot is a plain memcpy");

public:
    /** `capacity` is the number of elements the ring holds when full. */
    explicit SpscRing(int capacity)
        : slots(static_cast<std::size_t>(capacity) + 1) {} // +1: one slot stays empty by design

    // PRODUCER THREAD. Returns false (and drops `item`) when the ring is full.
    bool push(const T& item) noexcept AS_NONBLOCKING {
        const int write = writeIndex.load(std::memory_order_relaxed);
        const int next = advance(write);
        if (next == readIndex.load(std::memory_order_acquire))
            return false;
        slots[static_cast<std::size_t>(write)] = item;
        writeIndex.store(next, std::memory_order_release);
        return true;
    }

    // CONSUMER THREAD. Invokes `fn(const T&)` once per queued element, oldest first, then leaves the
    // ring empty (of everything queued when the call started).
    template <typename Fn>
    void drain(Fn&& fn) {
        int read = readIndex.load(std::memory_order_relaxed);
        const int write = writeIndex.load(std::memory_order_acquire);
        while (read != write) {
            fn(slots[static_cast<std::size_t>(read)]);
            read = advance(read);
        }
        readIndex.store(read, std::memory_order_release);
    }

    // Either thread; exact for the consumer, a lower bound for the producer.
    int getNumReady() const noexcept {
        const int write = writeIndex.load(std::memory_order_acquire);
        const int read = readIndex.load(std::memory_order_relaxed);
        const int size = static_cast<int>(slots.size());
        return write >= read ? write - read : size - (read - write);
    }

    // CONSUMER THREAD, and only while the producer is known to be idle (e.g. disarmed): drops
    // everything queued.
    void reset() noexcept {
        readIndex.store(0, std::memory_order_relaxed);
        writeIndex.store(0, std::memory_order_release);
    }

private:
    int advance(int index) const noexcept { return index + 1 == static_cast<int>(slots.size()) ? 0 : index + 1; }

    std::vector<T> slots;
    std::atomic<int> writeIndex{0};
    std::atomic<int> readIndex{0};

    static_assert(std::atomic<int>::is_always_lock_free, "a realtime thread may not take a lock to queue");
};

} // namespace synth
