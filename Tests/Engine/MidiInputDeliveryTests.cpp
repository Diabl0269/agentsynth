// MidiInputDeliveryTests.cpp
//
// FRO279: a real message arriving from a real OS MIDI source must reach
// AudioEngine::handleIncomingMidiMessageFromSource exactly ONCE. The headless MIDI Remote tests
// call RemoteEngine::handleMessage directly, one call per message, so they cannot see a second
// delivery that happens upstream of the engine (an input opened twice). This drives the real
// delivery path: an in-process virtual CoreMIDI/ALSA source (juce::MidiOutput::createNewDevice),
// opened by the engine the way MIDI Remote opens a profile's controller.
//
// Needs a working OS MIDI service, which a bare CI container may lack: every test skips (not
// fails) when the virtual device cannot be created or does not enumerate.

#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <thread>

namespace {

class CountingSink : public synth::midi::RemoteMessageSink {
public:
    bool handleMessage(const juce::String&, const juce::MidiMessage&) noexcept override {
        ++calls;
        return true;
    }
    std::atomic<int> calls{0};
};

/** Exposes the launch loop's own open call: initialiseDevices() opens every available input through
 *  openMidiInput, and doing that here avoids opening the real audio device initialise() would. */
class LaunchLoopEngine : public AudioEngine {
public:
    using AudioEngine::AudioEngine;
    void runLaunchLoop() {
        for (auto& info : availableMidiInputs())
            openMidiInput(info);
    }
};

/** JUCE only offers virtual MIDI devices off Windows (MidiOutput::createNewDevice is compiled out
 *  there); nullptr makes the tests skip. */
std::unique_ptr<juce::MidiOutput> makeVirtualSource(const juce::String& name) {
#if JUCE_WINDOWS
    juce::ignoreUnused(name);
    return nullptr;
#else
    return juce::MidiOutput::createNewDevice(name);
#endif
}

/** Waits (bounded) for the driver thread to deliver; the OS MIDI callback is asynchronous. */
void settle(const std::atomic<int>& counter, int atLeast) {
    for (int i = 0; i < 100 && counter.load() < atLeast; ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    // A duplicate delivery lands right behind the first; give it room to show up.
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
}

} // namespace

TEST(MidiInputDeliveryTest, OneMessageFromAnOpenedSourceIsDeliveredOnce) {
    const juce::String name = "FRO279 Delivery Test Source";
    auto virtualSource = makeVirtualSource(name);
    if (virtualSource == nullptr)
        GTEST_SKIP() << "no OS MIDI service: cannot create a virtual source";

    AudioEngine engine(AudioEngine::HostMode::Standalone);
    CountingSink sink;
    engine.setRemoteMessageSink(&sink);

    engine.openMidiDevicesForRemote({name});
    if (engine.getOpenMidiInputIdentifiers().empty())
        GTEST_SKIP() << "the virtual source did not enumerate as an input";
    EXPECT_EQ(engine.getOpenMidiInputIdentifiers().size(), 1u);

    virtualSource->sendMessageNow(juce::MidiMessage::controllerEvent(1, 23, 127));
    settle(sink.calls, 1);

    EXPECT_EQ(sink.calls.load(), 1);
    engine.setRemoteMessageSink(nullptr);
    engine.drainRemoteSinkCalls();
}

// MainComponent::wireMidiRemoteEngine opens a profile's controller by name BEFORE
// initialiseAudioEngine() runs initialiseDevices()'s open-everything loop, so a controller with a
// saved profile is reached by both. It must still be one open input, or every message is applied
// twice (a relative encoder at double speed, a toggle that flips back).
TEST(MidiInputDeliveryTest, ProfileOpenFollowedByLaunchLoopOpensTheSourceOnce) {
    const juce::String name = "FRO279 Launch Order Test Source";
    auto virtualSource = makeVirtualSource(name);
    if (virtualSource == nullptr)
        GTEST_SKIP() << "no OS MIDI service: cannot create a virtual source";

    LaunchLoopEngine engine(AudioEngine::HostMode::Standalone);
    CountingSink sink;
    engine.setRemoteMessageSink(&sink);

    engine.openMidiDevicesForRemote({name});
    if (engine.getOpenMidiInputIdentifiers().empty())
        GTEST_SKIP() << "the virtual source did not enumerate as an input";
    engine.runLaunchLoop();

    const auto identifiers = engine.getOpenMidiInputIdentifiers();
    EXPECT_EQ(std::count(identifiers.begin(), identifiers.end(), identifiers.front()), 1);

    virtualSource->sendMessageNow(juce::MidiMessage::controllerEvent(1, 23, 127));
    settle(sink.calls, 1);

    EXPECT_EQ(sink.calls.load(), 1);
    engine.setRemoteMessageSink(nullptr);
    engine.drainRemoteSinkCalls();
}
