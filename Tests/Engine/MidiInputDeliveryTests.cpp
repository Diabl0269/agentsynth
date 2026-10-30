// MidiInputDeliveryTests.cpp
//
// A real message arriving from a real OS MIDI source must reach
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

/** A per-run source name no other process can share: virtual sources are visible machine-wide, so two test
 *  processes (parallel shards, another session) creating the same name would see each other's. */
juce::String uniqueSourceName(const juce::String& base) {
    return base + " " + juce::Uuid().toString().substring(0, 8);
}

/** The OS publishes a new virtual source to the input list asynchronously, and the first CoreMIDI
 *  use in a process takes the longest. Polls (bounded) until the source is enumerable. */
bool waitUntilEnumerated(const juce::String& name) {
    const auto deadline = juce::Time::getMillisecondCounter() + 5000;
    for (;;) {
        for (auto& info : juce::MidiInput::getAvailableDevices())
            if (info.name == name)
                return true;
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        // CoreMIDI refreshes its source list from the run loop of the thread that first used it.
        if (juce::MessageManager::getInstance()->isThisTheMessageThread())
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        else
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

juce::String identifierOf(const juce::String& name) {
    for (auto& info : juce::MidiInput::getAvailableDevices())
        if (info.name == name)
            return info.identifier;
    return {};
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
    const juce::String name = uniqueSourceName("FRO279 Delivery Test Source");
    auto virtualSource = makeVirtualSource(name);
    if (virtualSource == nullptr)
        GTEST_SKIP() << "no OS MIDI service: cannot create a virtual source";
    if (!waitUntilEnumerated(name))
        GTEST_SKIP() << "the virtual source did not enumerate as an input";

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

// MainComponent::wireMidiRemoteEngine can open a profile's controller by name BEFORE
// initialiseAudioEngine() runs initialiseDevices()'s open-everything loop, so a controller with
// a saved profile is reached by both. The identifier-keyed dedupe in AudioEngine::openMidiInput
// must keep this a single open input regardless of which side runs first (a relative encoder at
// double speed, a toggle that flips back, otherwise) -- this test covers that ordering.
TEST(MidiInputDeliveryTest, ProfileOpenFollowedByLaunchLoopOpensTheSourceOnce) {
    const juce::String name = uniqueSourceName("FRO279 Launch Order Test Source");
    auto virtualSource = makeVirtualSource(name);
    if (virtualSource == nullptr)
        GTEST_SKIP() << "no OS MIDI service: cannot create a virtual source";
    if (!waitUntilEnumerated(name))
        GTEST_SKIP() << "the virtual source did not enumerate as an input";

    LaunchLoopEngine engine(AudioEngine::HostMode::Standalone);
    CountingSink sink;
    engine.setRemoteMessageSink(&sink);

    engine.openMidiDevicesForRemote({name});
    if (engine.getOpenMidiInputIdentifiers().empty())
        GTEST_SKIP() << "the virtual source did not enumerate as an input";
    engine.runLaunchLoop();

    const auto identifiers = engine.getOpenMidiInputIdentifiers();
    EXPECT_EQ(std::count(identifiers.begin(), identifiers.end(), identifierOf(name)), 1);

    virtualSource->sendMessageNow(juce::MidiMessage::controllerEvent(1, 23, 127));
    settle(sink.calls, 1);

    EXPECT_EQ(sink.calls.load(), 1);
    engine.setRemoteMessageSink(nullptr);
    engine.drainRemoteSinkCalls();
}

// MainComponent::openMidiRemoteDevices() (the profile-open half split out of
// wireMidiRemoteEngine) now runs AFTER initialiseAudioEngine() has brought the engine up, so
// initialiseDevices()'s open-everything loop is the one that runs FIRST in production -- the
// mirror image of the historical ordering above. Same dedupe, same single-delivery guarantee,
// covering the order MainComponent actually uses today.
TEST(MidiInputDeliveryTest, LaunchLoopFollowedByProfileOpenOpensTheSourceOnce) {
    const juce::String name = uniqueSourceName("FRO260 Launch Order Test Source");
    auto virtualSource = makeVirtualSource(name);
    if (virtualSource == nullptr)
        GTEST_SKIP() << "no OS MIDI service: cannot create a virtual source";
    if (!waitUntilEnumerated(name))
        GTEST_SKIP() << "the virtual source did not enumerate as an input";

    LaunchLoopEngine engine(AudioEngine::HostMode::Standalone);
    CountingSink sink;
    engine.setRemoteMessageSink(&sink);

    engine.runLaunchLoop();
    if (engine.getOpenMidiInputIdentifiers().empty())
        GTEST_SKIP() << "the virtual source did not enumerate as an input";
    engine.openMidiDevicesForRemote({name});

    const auto identifiers = engine.getOpenMidiInputIdentifiers();
    EXPECT_EQ(std::count(identifiers.begin(), identifiers.end(), identifierOf(name)), 1);

    virtualSource->sendMessageNow(juce::MidiMessage::controllerEvent(1, 23, 127));
    settle(sink.calls, 1);

    EXPECT_EQ(sink.calls.load(), 1);
    engine.setRemoteMessageSink(nullptr);
    engine.drainRemoteSinkCalls();
}
