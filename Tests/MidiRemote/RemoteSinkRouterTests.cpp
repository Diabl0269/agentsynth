// RemoteSinkRouter (docs/control/midi-remote-mcu-hui.md#1-one-seam-routed-by-source): a port bound to a protocol
// surface is consumed whole, every other port reaches RemoteEngine unchanged. The threading case is one of the suites
// meant to be run under ThreadSanitizer (docs/development/test-patterns.md#sanitizers).

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/Surface/McuSurface.h"
#include "MidiRemote/Surface/RemoteSinkRouter.h"

#include <atomic>
#include <gtest/gtest.h>
#include <thread>
#include <vector>

using namespace synth;
using namespace synth::midi;

namespace {

class RecordingSink : public RemoteMessageSink {
public:
    bool handleMessage(const juce::String&, const juce::MidiMessage&) noexcept override {
        calls.fetch_add(1, std::memory_order_relaxed);
        return consume;
    }
    std::atomic<int> calls{0};
    bool consume = false;
};

int drainCount(ProtocolSurface& surface) {
    int n = 0;
    surface.drain([&](const SurfaceEvent&) { ++n; });
    return n;
}

const juce::uint8 kSysex[] = {0xF0, 0x00, 0x00, 0x66, 0x14, 0x01, 0xF7};

} // namespace

TEST(MidiRemoteSinkRouter, NoteCcAndSysexFromAnOwnedPortAreConsumedAndNeverReachTheFallback) {
    RecordingSink fallback;
    RemoteSinkRouter router(fallback);
    auto surface = std::make_shared<McuSurface>();
    router.setOwnedSources({{"mcu-port", surface}});

    EXPECT_TRUE(router.handleMessage("mcu-port", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)));
    EXPECT_TRUE(router.handleMessage("mcu-port", juce::MidiMessage::controllerEvent(1, 7, 64)));
    EXPECT_TRUE(router.handleMessage("mcu-port", juce::MidiMessage(kSysex, sizeof(kSysex))));
    EXPECT_TRUE(router.handleMessage("mcu-port", juce::MidiMessage::channelPressureChange(1, 3)))
        << "even a message the protocol ignores is consumed";
    EXPECT_EQ(fallback.calls.load(), 0);
    EXPECT_EQ(drainCount(*surface), 2) << "the note and the sysex decode; CC 7 and pressure mean nothing to an MCU";
}

TEST(MidiRemoteSinkRouter, UnownedPortsGoToTheFallbackWithItsAnswer) {
    RecordingSink fallback;
    RemoteSinkRouter router(fallback);
    router.setOwnedSources({{"mcu-port", std::make_shared<McuSurface>()}});

    EXPECT_FALSE(router.handleMessage("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100)));
    fallback.consume = true;
    EXPECT_TRUE(router.handleMessage("keyboard", juce::MidiMessage::controllerEvent(1, 7, 64)));
    EXPECT_EQ(fallback.calls.load(), 2);

    RemoteSinkRouter empty(fallback);
    EXPECT_TRUE(empty.handleMessage("mcu-port", juce::MidiMessage::noteOn(1, 60, (juce::uint8)1)))
        << "before any table is published everything is the fallback's";
    EXPECT_EQ(fallback.calls.load(), 3);
}

TEST(MidiRemoteSinkRouter, OverLongSysexFromAnOwnedPortIsConsumedAndDropped) {
    RecordingSink fallback;
    RemoteSinkRouter router(fallback);
    auto surface = std::make_shared<McuSurface>();
    router.setOwnedSources({{"mcu-port", surface}});
    std::vector<juce::uint8> longSysex(40, 0x10);
    longSysex.front() = 0xF0;
    longSysex.back() = 0xF7;
    EXPECT_TRUE(router.handleMessage("mcu-port", juce::MidiMessage(longSysex.data(), (int)longSysex.size())));
    EXPECT_EQ(drainCount(*surface), 0);
    EXPECT_EQ(surface->droppedCount(), 1);
    EXPECT_EQ(fallback.calls.load(), 0);
}

TEST(MidiRemoteSinkRouter, UnbindingAPortHandsItBackToTheFallback) {
    RecordingSink fallback;
    RemoteSinkRouter router(fallback);
    router.setOwnedSources({{"mcu-port", std::make_shared<McuSurface>()}});
    EXPECT_TRUE(router.owns("mcu-port"));
    router.setOwnedSources({});
    EXPECT_FALSE(router.owns("mcu-port"));
    router.handleMessage("mcu-port", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    EXPECT_EQ(fallback.calls.load(), 1);
}

// Through the real AudioEngine seam: an owned port's note never reaches RemoteEngine, so a learn
// armed on the engine cannot hear the Mackie Control.
TEST(MidiRemoteSinkRouter, InstalledAsTheEnginesSinkAnOwnedPortNeverReachesRemoteEngine) {
    AudioEngine engine{AudioEngine::HostMode::Hosted};
    RemoteEngine remote;
    RemoteSinkRouter router(remote);
    auto surface = std::make_shared<McuSurface>();
    router.setOwnedSources({{"mcu-port", surface}});
    remote.setSources({juce::String("mcu-port"), juce::String("keyboard")});
    engine.setRemoteMessageSink(&router);

    engine.handleIncomingMidiMessageFromSource("mcu-port", juce::MidiMessage::pitchWheel(1, 9000));
    int activity = 0;
    remote.drainActivity([&](const juce::String&, const RemoteEvent&) { ++activity; });
    EXPECT_EQ(activity, 0);
    EXPECT_EQ(drainCount(*surface), 1);
    engine.setRemoteMessageSink(nullptr);
}

// Two producer threads (one owned port, one not) hammer the router while the message thread
// republishes the owned table and collects retired ones. Under TSan this proves the table and the
// surface it owns are never read after free and the fallback path is never torn.
TEST(MidiRemoteSinkRouter, RepublishDuringTrafficFromTwoPortsIsRaceFree) {
    RecordingSink fallback;
    RemoteSinkRouter router(fallback);
    auto surface = std::make_shared<McuSurface>();
    router.setOwnedSources({{"mcu-port", surface}});

    std::atomic<bool> stop{false};
    std::atomic<int> owned{0};
    std::thread mcuThread([&] {
        while (!stop.load(std::memory_order_relaxed))
            if (router.handleMessage("mcu-port", juce::MidiMessage::pitchWheel(1, 4000)))
                owned.fetch_add(1, std::memory_order_relaxed);
    });
    std::thread keyboardThread([&] {
        while (!stop.load(std::memory_order_relaxed))
            router.handleMessage("keyboard", juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
    });

    int drained = 0;
    for (int i = 0; i < 2000; ++i) {
        router.setOwnedSources({{"mcu-port", surface}}); // same surface, a fresh table each time
        router.collectRetired();
        surface->drain([&](const SurfaceEvent&) { ++drained; });
    }
    stop.store(true);
    mcuThread.join();
    keyboardThread.join();
    router.collectRetired();
    surface->drain([&](const SurfaceEvent&) { ++drained; });

    EXPECT_GT(owned.load(), 0);
    EXPECT_GT(fallback.calls.load(), 0);
    EXPECT_EQ(router.publisher().retiredCount(), 0);
    EXPECT_EQ(drained + surface->droppedCount(), owned.load()) << "every owned message is queued or counted dropped";
}
