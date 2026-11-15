// The cross-thread contract of synth::SpscRing and of the rings built on it (AutomationUiFeed, the
// MIDI Remote lanes). Meant to be run under ThreadSanitizer as well as normally: a payload handoff
// that is not provably ordered shows up there as a data race on the slot, which is what
// juce::AbstractFifo did (docs/development/test-patterns.md, "ThreadSanitizer").

#include "AudioEngine/SpscRing.h"
#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "Timeline/AutomationUiFeed.h"

#include <atomic>
#include <cstdint>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using synth::AutomationUiEvent;
using synth::AutomationUiFeed;
using synth::SpscRing;

namespace {

struct Payload {
    std::uint64_t a = 0;
    std::uint64_t b = 0; // always ~a: a torn read shows as a mismatch even without TSan
    double c = 0.0;
};

constexpr int kItems = 200000;

} // namespace

TEST(SpscRingTest, HoldsExactlyCapacityThenDropsTheNewest) {
    SpscRing<int> ring(4);
    for (int i = 0; i < 4; ++i)
        EXPECT_TRUE(ring.push(i));
    EXPECT_FALSE(ring.push(99));
    EXPECT_EQ(ring.getNumReady(), 4);

    std::vector<int> seen;
    ring.drain([&seen](int v) { seen.push_back(v); });
    EXPECT_EQ(seen, (std::vector<int>{0, 1, 2, 3}));
    EXPECT_EQ(ring.getNumReady(), 0);
    EXPECT_TRUE(ring.push(5)); // wraps and keeps working
}

TEST(SpscRingTest, ResetDropsEverythingQueued) {
    SpscRing<int> ring(8);
    ring.push(1);
    ring.push(2);
    ring.reset();
    EXPECT_EQ(ring.getNumReady(), 0);
    int count = 0;
    ring.drain([&count](int) { ++count; });
    EXPECT_EQ(count, 0);
}

TEST(SpscRingTest, OneProducerOneConsumerDeliversEveryItemInOrderIntact) {
    SpscRing<Payload> ring(64);
    std::atomic<bool> go{false};

    std::thread producer([&] {
        while (!go.load(std::memory_order_acquire)) {
        }
        for (int i = 0; i < kItems; ++i) {
            const Payload p{static_cast<std::uint64_t>(i), ~static_cast<std::uint64_t>(i), i * 0.5};
            while (!ring.push(p))
                std::this_thread::yield();
        }
    });

    std::uint64_t expected = 0;
    bool intact = true;
    go.store(true, std::memory_order_release);
    while (expected < static_cast<std::uint64_t>(kItems)) {
        ring.drain([&](const Payload& p) {
            if (p.a != expected || p.b != ~expected || p.c != static_cast<double>(expected) * 0.5)
                intact = false;
            ++expected;
        });
        std::this_thread::yield();
    }
    producer.join();
    EXPECT_TRUE(intact);
    EXPECT_EQ(expected, static_cast<std::uint64_t>(kItems));
}

TEST(AutomationUiFeedThreadingTest, AudioToMessageHandoffNeverTearsAnEvent) {
    AutomationUiFeed feed;
    std::atomic<bool> done{false};

    std::thread audio([&] {
        for (int i = 1; i <= kItems; ++i) {
            AutomationUiEvent e;
            e.nodeId = static_cast<juce::uint32>(i);
            e.newNormalized = static_cast<float>(i);
            feed.push(e); // drops on full, like the audio thread
        }
        done.store(true, std::memory_order_release);
    });

    juce::uint32 last = 0;
    bool intact = true;
    auto drainOnce = [&] {
        feed.drain([&](const AutomationUiEvent& e) {
            if (e.nodeId <= last || e.newNormalized != static_cast<float>(e.nodeId))
                intact = false; // reordered or torn
            last = e.nodeId;
        });
    };
    while (!done.load(std::memory_order_acquire))
        drainOnce();
    audio.join();
    drainOnce();
    EXPECT_TRUE(intact);
    EXPECT_GT(last, 0u);
}

TEST(RemoteEventFifoThreadingTest, MidiThreadToMessageThreadHandoffNeverTearsAnEvent) {
    synth::midi::RemoteEventFifo fifo(256);
    std::thread midi([&] {
        for (int i = 1; i <= kItems; ++i) {
            synth::midi::RemoteEvent e;
            e.slotIndex = i;
            e.value = static_cast<float>(i);
            while (fifo.getNumReady() >= 256)
                std::this_thread::yield();
            fifo.push(e);
        }
    });
    int next = 1;
    bool intact = true;
    while (next <= kItems) {
        fifo.drain([&](const synth::midi::RemoteEvent& e) {
            if (e.slotIndex != next || e.value != static_cast<float>(next))
                intact = false;
            ++next;
        });
        std::this_thread::yield();
    }
    midi.join();
    EXPECT_TRUE(intact);
}
