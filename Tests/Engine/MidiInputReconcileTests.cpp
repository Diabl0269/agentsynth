// MidiInputReconcileTests.cpp
//
// FRO262: a MIDI input device ticked in Settings > Audio's MIDI Input list AFTER app launch (a
// controller plugged in later, or one the OS enumerates late) must become visible to MIDI
// Learn/MIDI Remote and to general MIDI input, not just whatever was available when
// AudioEngine::initialiseDevices()'s one-shot launch loop ran. AudioEngine::changeListenerCallback
// now calls reconcileMidiInputs() (Source/AudioEngine/AudioEngineMidi.cpp) on every
// AudioDeviceManager change broadcast to keep midiInputs in step with the Audio tab's checkboxes.
//
// juce::MidiInput's constructor is private (only openDevice()/createNewDevice() can make one, and
// both talk to real OS MIDI drivers -- see Tests/Engine/BounceExporter/BounceExporterTransportTests.cpp's
// own note on this), so the "a ticked device actually ends up open" leg cannot be proven headlessly
// here -- see the file-level note in each test below for exactly what IS provable without hardware.
//
// House rules as everywhere else (Tests/Engine/AudioInputTests.cpp): no real audio/MIDI device
// opened, no sleeps, no network. A HostMode::Standalone engine here never has initialise() called
// on it, since that opens a real audio device; changeListenerCallback is invoked directly instead
// of relying on juce::ChangeBroadcaster's async dispatch, the same way other tests in this repo
// drive a callback method directly rather than pumping a message loop.

#include "AudioEngine/AudioEngine.h"
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>

namespace {

/** AudioEngine with the "what MIDI devices does JUCE see as connected" query intercepted -- the
 *  same seam pattern as SeamEngine (Tests/Engine/AudioInputTests.cpp) overriding
 *  initialiseDevices(), but for reconcileMidiInputs()'s own device-list query (availableMidiInputs()),
 *  so a test can inject devices without a real controller attached to the machine running it. */
class MidiSeamEngine : public AudioEngine {
public:
    using AudioEngine::AudioEngine;

    std::vector<juce::MidiDeviceInfo> fakeDevices;

protected:
    juce::Array<juce::MidiDeviceInfo> availableMidiInputs() const override {
        juce::Array<juce::MidiDeviceInfo> result;
        for (auto& info : fakeDevices)
            result.add(info);
        return result;
    }
};

juce::MidiDeviceInfo makeFakeDevice(const juce::String& name, const juce::String& identifier) {
    juce::MidiDeviceInfo info;
    info.name = name;
    info.identifier = identifier;
    return info;
}

} // namespace

// ============================================================================
// HostMode::Hosted never touches the device list (Source/CLAUDE.md's MIDI-path tripwire)
// ============================================================================

TEST(MidiInputReconcileTest, HostedModeNeverOpensAnythingEvenWhenTicked) {
    // Ticking a fake device AND running the reconcile must still be a complete no-op in Hosted
    // mode -- changeListenerCallback's own isHosted() guard makes reconcileMidiInputs()
    // structurally unreachable, so this proves the guard, not merely "nothing matched".
    MidiSeamEngine engine(AudioEngine::HostMode::Hosted);
    engine.fakeDevices.push_back(makeFakeDevice("Fake Controller", "fake-id-1"));
    engine.getDeviceManager().setMidiInputDeviceEnabled("fake-id-1", true);

    engine.changeListenerCallback(&engine.getDeviceManager());

    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty());
}

// ============================================================================
// The callback only ever reacts to its own device manager
// ============================================================================

TEST(MidiInputReconcileTest, WrongBroadcasterIsIgnored) {
    // changeListenerCallback is only ever subscribed to the engine's own deviceManager; a stray
    // call with a different broadcaster must not run the reconcile at all.
    MidiSeamEngine engine(AudioEngine::HostMode::Standalone);
    engine.fakeDevices.push_back(makeFakeDevice("Fake Controller", "fake-id-1"));
    engine.getDeviceManager().setMidiInputDeviceEnabled("fake-id-1", true);

    juce::ChangeBroadcaster unrelated;
    engine.changeListenerCallback(&unrelated);

    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty());
}

// ============================================================================
// Open is additive: only a ticked device is ever a candidate
// ============================================================================

TEST(MidiInputReconcileTest, UntickedDeviceIsNeverOpened) {
    MidiSeamEngine engine(AudioEngine::HostMode::Standalone);
    engine.fakeDevices.push_back(makeFakeDevice("Fake Controller", "fake-id-1"));
    // Deliberately never ticked in the device manager.

    engine.changeListenerCallback(&engine.getDeviceManager());

    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty());
}

TEST(MidiInputReconcileTest, TickedDeviceThatCannotActuallyOpenIsASafeNoOp) {
    // A bogus identifier can never resolve to a real juce::MidiInput (openDevice() returns
    // nullptr for it), so this cannot prove a REAL device ends up open -- only that ticking one,
    // then running the reconcile, never throws/crashes and leaves the open set exactly where a
    // failed open leaves it. See the file header for why the positive "it actually opened" leg
    // cannot be proven headlessly.
    MidiSeamEngine engine(AudioEngine::HostMode::Standalone);
    engine.fakeDevices.push_back(makeFakeDevice("Fake Controller", "Definitely-Not-A-Real-Identifier-92348"));
    engine.getDeviceManager().setMidiInputDeviceEnabled("Definitely-Not-A-Real-Identifier-92348", true);

    EXPECT_NO_THROW(engine.changeListenerCallback(&engine.getDeviceManager()));
    EXPECT_TRUE(engine.getOpenMidiInputIdentifiers().empty());
}

// ============================================================================
// onMidiDevicesChanged only fires when the open set actually changed
// ============================================================================

TEST(MidiInputReconcileTest, NothingToDoNeverFiresOnMidiDevicesChanged) {
    // A pure sample-rate/buffer-size change also broadcasts through this same
    // changeListenerCallback -- the hook must not fire (and MidiLearnController::refreshSources()
    // must not re-run) when reconcileMidiInputs() found nothing to open or close.
    MidiSeamEngine engine(AudioEngine::HostMode::Standalone);
    bool fired = false;
    engine.onMidiDevicesChanged = [&] { fired = true; };

    engine.changeListenerCallback(&engine.getDeviceManager());

    EXPECT_FALSE(fired);
}

TEST(MidiInputReconcileTest, HostedModeNeverFiresOnMidiDevicesChanged) {
    MidiSeamEngine engine(AudioEngine::HostMode::Hosted);
    engine.fakeDevices.push_back(makeFakeDevice("Fake Controller", "fake-id-1"));
    engine.getDeviceManager().setMidiInputDeviceEnabled("fake-id-1", true);
    bool fired = false;
    engine.onMidiDevicesChanged = [&] { fired = true; };

    engine.changeListenerCallback(&engine.getDeviceManager());

    EXPECT_FALSE(fired);
}
