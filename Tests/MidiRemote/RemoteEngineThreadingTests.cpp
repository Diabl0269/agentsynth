// The tripwire tests for docs/midi_remote.md §4.4: the mapping table crosses threads and the
// engine may never take a lock, allocate or free something a reader still holds. Suite name
// contains "MidiRemote" so it matches the ship-task verification filter.
//
// These are the tests that are meant to be run under ThreadSanitizer as well as normally —
// CI's label-gated sanitizer job builds with -fsanitize=address only, so a TSan run is a local
// gate (see docs/development/test-patterns.md).

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"

#include <atomic>
#include <gtest/gtest.h>
#include <memory>
#include <thread>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

constexpr int kDevices = 4;
constexpr int kControlsPerDevice = 16;
constexpr int kFirstCc = 20;

juce::String deviceKey(int index) { return "dev" + juce::String(index); }
juce::String profileId(int index) { return "p" + juce::String(index); }
juce::String controlId(int index) { return "c" + juce::String(index); }

std::vector<ControllerProfile> makeProfiles() {
    std::vector<ControllerProfile> profiles;
    for (int d = 0; d < kDevices; ++d) {
        ControllerProfile profile;
        profile.id = profileId(d);
        profile.name = profile.id;
        profile.input.identifier = deviceKey(d);
        profile.input.name = deviceKey(d);
        for (int c = 0; c < kControlsPerDevice; ++c) {
            Control control;
            control.id = controlId(c);
            control.name = control.id;
            control.kind = ControlKind::knob;
            control.encoding = Encoding::abs7;
            control.message.type = MessageType::cc;
            control.message.channel = 1;
            control.message.number = kFirstCc + c;
            profile.controls.push_back(control);
        }
        profiles.push_back(profile);
    }
    return profiles;
}

// `generation` only changes the target node uuid, so every rebuild produces a genuinely different
// snapshot object that the previous one has to be retired for.
std::vector<Assignment> makeAssignments(int generation) {
    std::vector<Assignment> out;
    for (int d = 0; d < kDevices; ++d) {
        for (int c = 0; c < kControlsPerDevice; ++c) {
            Assignment assignment;
            assignment.id = "a" + juce::String(d) + "-" + juce::String(c);
            assignment.control.profileId = profileId(d);
            assignment.control.controlId = controlId(c);
            assignment.spec.type = MessageType::cc;
            assignment.spec.channel = 1;
            assignment.spec.number = kFirstCc + c;
            assignment.specEncoding = Encoding::abs7;
            assignment.target.kind = Target::Kind::parameter;
            assignment.target.parameter.nodeUuid = "node-" + juce::String(generation);
            assignment.target.parameter.paramId = "gain";
            out.push_back(assignment);
        }
    }
    return out;
}

std::vector<juce::String> allDeviceKeys() {
    std::vector<juce::String> keys;
    for (int d = 0; d < kDevices; ++d)
        keys.push_back(deviceKey(d));
    return keys;
}

} // namespace

// A retired snapshot may not be freed while any reader still holds it, and must be freed once the
// last reader leaves. This is the whole correctness argument of SnapshotPublisher, asserted
// directly rather than left to a sanitizer to catch by luck.
TEST(MidiRemoteSnapshotPublisher, RetiredSnapshotOutlivesAnInFlightReader) {
    SnapshotPublisher publisher;
    publisher.publish(std::make_unique<const RemoteMappingSnapshot>());

    {
        SnapshotPublisher::ReadGuard guard(publisher);
        ASSERT_NE(guard.get(), nullptr);

        publisher.publish(std::make_unique<const RemoteMappingSnapshot>());
        publisher.collectRetired();
        EXPECT_EQ(publisher.retiredCount(), 1) << "freed a snapshot while a reader was inside it";

        // Still readable: this is the dereference that would be a use-after-free if the collector
        // had freed it above.
        EXPECT_TRUE(guard.get()->isEmpty());
    }

    publisher.collectRetired();
    EXPECT_EQ(publisher.retiredCount(), 0) << "retired snapshots leak once every reader has left";
}

TEST(MidiRemoteSnapshotPublisher, ReadersNeverSeeATornOrFreedTable) {
    SnapshotPublisher publisher;

    const auto makeSnapshot = [](int slotCount) {
        auto snapshot = std::make_unique<RemoteMappingSnapshot>();
        for (int i = 0; i < slotCount; ++i) {
            RemoteMappingSnapshot::Slot slot;
            slot.assignmentId = "a" + juce::String(i);
            snapshot->slots.push_back(slot);
        }
        return std::unique_ptr<const RemoteMappingSnapshot>(snapshot.release());
    };

    publisher.publish(makeSnapshot(1));

    std::atomic<bool> stop{false};
    std::atomic<long> reads{0};
    std::vector<std::thread> readers;
    for (int t = 0; t < kDevices; ++t) {
        readers.emplace_back([&] {
            while (!stop.load(std::memory_order_relaxed)) {
                SnapshotPublisher::ReadGuard guard(publisher);
                if (const auto* snapshot = guard.get(); snapshot != nullptr) {
                    // Touch the heap the publisher is recycling.
                    const auto count = snapshot->slots.size();
                    if (count > 0)
                        (void)snapshot->slots[count - 1].assignmentId.length();
                    reads.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }

    for (int generation = 1; generation <= 500; ++generation) {
        publisher.publish(makeSnapshot(1 + (generation % 8)));
        publisher.collectRetired();
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& reader : readers)
        reader.join();

    publisher.collectRetired();
    EXPECT_GT(reads.load(), 0);
    EXPECT_EQ(publisher.retiredCount(), 0);
}

// The burst-while-swapping test the ticket asks for: several MIDI device threads hammering
// handleMessage while the message thread republishes the table and drains. Standalone really does
// deliver on one thread per juce::MidiInput, which is why the lanes are per-source and the table is
// read under a reader count rather than through a single-consumer exchange.
TEST(MidiRemoteEngineThreading, BurstWhileSwappingTheTable) {
    RemoteEngine engine;
    engine.setProfiles(makeProfiles());
    engine.setSources(allDeviceKeys());
    engine.setAssignments(makeAssignments(0));

    std::atomic<bool> stop{false};
    std::atomic<long> sent{0};

    std::vector<std::thread> producers;
    for (int d = 0; d < kDevices; ++d) {
        producers.emplace_back([&, d] {
            const juce::String key = deviceKey(d); // built once: the MIDI path allocates nothing
            int step = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                const auto message =
                    juce::MidiMessage::controllerEvent(1, kFirstCc + (step % kControlsPerDevice), step % 128);
                engine.handleMessage(key, message);
                ++step;
                sent.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Enough generations that a republish really does land mid-burst, and a floor on the work
    // done so this can never silently degrade into a test that proves nothing.
    for (int generation = 1; generation <= 2000; ++generation) {
        engine.setAssignments(makeAssignments(generation));
        engine.drain();
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& producer : producers)
        producer.join();

    engine.drain();
    engine.drain();

    EXPECT_GT(sent.load(), 10000) << "the burst was too small to have exercised a concurrent republish";
    EXPECT_EQ(engine.publisher().retiredCount(), 0)
        << "every snapshot retired during the burst should be reclaimable once the burst stops";
}

// FRO197: AudioEngine::setRemoteMessageSink(nullptr) must be safe to call concurrently with the
// threads that read remoteMessageSink_ and call into it OUTSIDE any render pass --
// AudioEngineMidi.cpp's handleIncomingMidiMessageFromSource (the standalone juce::MidiInput driver
// thread's real entry point) is exactly that call site. Before ScopedRemoteSinkCall /
// drainRemoteSinkCalls() existed, a producer thread here could load the old (non-null) sink pointer
// just before this loop clears and destroys it, then dereference a dead RemoteEngine a moment later
// -- a nanosecond-wide use-after-free at teardown. This hammers that window directly through the
// real seam, repeatedly, so a regression shows up as a crash (and reliably under ThreadSanitizer;
// see the file header) rather than passing by luck on a quiet machine.
TEST(MidiRemoteEngineThreading, TeardownSurvivesConcurrentMidiThreadDelivery) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);

    std::atomic<bool> stop{false};
    std::atomic<long> delivered{0};

    std::vector<std::thread> producers;
    for (int d = 0; d < kDevices; ++d) {
        producers.emplace_back([&, d] {
            const juce::String key = deviceKey(d); // built once: the MIDI path allocates nothing
            int step = 0;
            while (!stop.load(std::memory_order_relaxed)) {
                const auto message =
                    juce::MidiMessage::controllerEvent(1, kFirstCc + (step % kControlsPerDevice), step % 128);
                engine.handleIncomingMidiMessageFromSource(key, message);
                ++step;
                delivered.fetch_add(1, std::memory_order_relaxed);
            }
        });
    }

    // Repeatedly wire a fresh RemoteEngine in, clear it, and destroy it -- setRemoteMessageSink(nullptr)
    // must return only once every handleMessage call already in flight on the producer threads above
    // has left, or the unique_ptr reset below is a use-after-free waiting to happen.
    //
    // Bounded on BOTH a minimum iteration count and a minimum delivered-message floor, not a fixed
    // iteration count: constructing a RemoteEngine is expensive enough that a fast machine can clear
    // 300 iterations before the producer threads have delivered a meaningful burst, which would let
    // this pass without ever really contending the teardown window. kMaxIterations is only a safety
    // cap against a wedged producer thread stalling the test outright.
    constexpr int kMinIterations = 300;
    constexpr long kMinDelivered = 10000;
    constexpr int kMaxIterations = 20000;
    int iteration = 0;
    while (iteration < kMaxIterations &&
           (iteration < kMinIterations || delivered.load(std::memory_order_relaxed) < kMinDelivered)) {
        auto remote = std::make_unique<RemoteEngine>();
        remote->setProfiles(makeProfiles());
        remote->setSources(allDeviceKeys());
        remote->setAssignments(makeAssignments(iteration));
        engine.setRemoteMessageSink(remote.get());
        engine.setRemoteMessageSink(nullptr);
        remote.reset(); // the FRO197 use-after-free, if the handshake above were incomplete
        ++iteration;
    }

    stop.store(true, std::memory_order_relaxed);
    for (auto& producer : producers)
        producer.join();

    EXPECT_GT(delivered.load(), 10000) << "the burst was too small to have exercised the teardown window";
}

// A message from a source the engine has not been told about is ignored outright — never consumed,
// never queued. This is the window between a device opening and MainComponent calling setSources.
TEST(MidiRemoteEngineThreading, UnknownSourceIsNeverConsumed) {
    RemoteEngine engine;
    engine.setProfiles(makeProfiles());
    engine.setSources(allDeviceKeys());
    engine.setAssignments(makeAssignments(0));

    const auto message = juce::MidiMessage::controllerEvent(1, kFirstCc, 64);
    EXPECT_FALSE(engine.handleMessage("a-device-nobody-announced", message));
}
