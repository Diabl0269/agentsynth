// Automation-launch detection (synth::isNoAudioDeviceLaunch) and the AudioEngine flag it
// drives (setAudioDeviceDisabled/isAudioDeviceDisabled).
//
// Headless/deterministic house rules (docs/development/test-patterns.md): a HostMode::Standalone
// engine must never have initialise() called on it in a test, because that opens real hardware.
// This file is the deliberate, narrow exception the doc already carves out for a subclassed
// initialiseDevices() seam -- except here nothing is subclassed at all: with
// setAudioDeviceDisabled(true) set BEFORE initialise(), initialise() skips initialiseDevices()
// entirely (see AudioEngineDeviceLifecycle.cpp), so calling initialise() on a real
// HostMode::Standalone engine is exactly what proves no device is ever opened -- the assertions
// below (no current device, no receiving callback) are the proof, not a side effect to tolerate.

#include "AudioEngine/AudioEngine.h"
#include "Auth/KeychainTokenStore.h"
#include "AutomationLaunch.h"
#include <gtest/gtest.h>

namespace {

using synth::isNoAudioDeviceLaunch;

TEST(AutomationLaunchTest, FlagPresentReturnsTrue) {
    juce::StringArray args{"--no-audio-device"};
    EXPECT_TRUE(isNoAudioDeviceLaunch(args, {}));
}

TEST(AutomationLaunchTest, FlagAbsentAndNoEnvReturnsFalse) {
    juce::StringArray args{"--fullscreen"};
    EXPECT_FALSE(isNoAudioDeviceLaunch(args, {}));
}

TEST(AutomationLaunchTest, UnrelatedArgsReturnFalse) {
    juce::StringArray args{"--some-other-flag", "positional", "-x"};
    EXPECT_FALSE(isNoAudioDeviceLaunch(args, {}));
}

TEST(AutomationLaunchTest, EnvValueOneReturnsTrue) { EXPECT_TRUE(isNoAudioDeviceLaunch({}, "1")); }

TEST(AutomationLaunchTest, EnvValueTrueUppercaseReturnsTrue) { EXPECT_TRUE(isNoAudioDeviceLaunch({}, "TRUE")); }

TEST(AutomationLaunchTest, EnvValueYesWithWhitespaceReturnsTrue) { EXPECT_TRUE(isNoAudioDeviceLaunch({}, " yes ")); }

TEST(AutomationLaunchTest, EnvValueZeroReturnsFalse) { EXPECT_FALSE(isNoAudioDeviceLaunch({}, "0")); }

TEST(AutomationLaunchTest, EnvValueEmptyReturnsFalse) { EXPECT_FALSE(isNoAudioDeviceLaunch({}, "")); }

TEST(AutomationLaunchTest, EnvValueNoReturnsFalse) { EXPECT_FALSE(isNoAudioDeviceLaunch({}, "no")); }

TEST(AudioEngineNoAudioDeviceTest, IsAudioDeviceDisabledDefaultsToFalse) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    EXPECT_FALSE(engine.isAudioDeviceDisabled());
}

TEST(AudioEngineNoAudioDeviceTest, SetAudioDeviceDisabledIsObservable) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.setAudioDeviceDisabled(true);
    EXPECT_TRUE(engine.isAudioDeviceDisabled());
}

TEST(AudioEngineNoAudioDeviceTest, InitialiseNeverOpensADeviceWhenDisabled) {
    AudioEngine engine(AudioEngine::HostMode::Standalone);
    engine.setAudioDeviceDisabled(true);
    engine.initialise();

    // No device, no device type, no registered callback -- initialiseDevices() (the only part of
    // initialise() that touches hardware) was never called.
    EXPECT_EQ(engine.getDeviceManager().getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(engine.getDeviceManager().getCurrentDeviceTypeObject(), nullptr);
    EXPECT_FALSE(engine.isReceivingDeviceCallbacks());

    // The default patch is still built -- an automation launch gets a real, editable graph, just
    // with no device clocking it.
    EXPECT_GT(engine.getGraph().getNodes().size(), 0);

    engine.shutdown(); // Must stay clean with no device/callback ever attached.
}

} // namespace

// --- Keychain policy -------------------------------------------------------------------------

namespace {

// Restores the suite-wide in-memory switch (TestMain turns it on) however a test leaves it.
struct KeychainSwitchGuard {
    ~KeychainSwitchGuard() { synth::KeychainTokenStore::useInMemoryStoreForProcess(true); }
};

} // namespace

TEST(AutomationLaunchTest, NoKeychainFlagIsRequested) {
    juce::StringArray args{"--no-keychain"};
    EXPECT_TRUE(synth::isNoKeychainRequested(args, {}));
}

TEST(AutomationLaunchTest, NoKeychainEnvTruthyValuesAreRequested) {
    EXPECT_TRUE(synth::isNoKeychainRequested({}, "1"));
    EXPECT_TRUE(synth::isNoKeychainRequested({}, "TRUE"));
    EXPECT_TRUE(synth::isNoKeychainRequested({}, " yes "));
}

TEST(AutomationLaunchTest, NoKeychainAbsentIsNotRequested) {
    EXPECT_FALSE(synth::isNoKeychainRequested({}, {}));
    EXPECT_FALSE(synth::isNoKeychainRequested({}, "0"));
    EXPECT_FALSE(synth::isNoKeychainRequested(juce::StringArray{"--other"}, "no"));
}

TEST(AutomationLaunchTest, AutomationLaunchIsKeychainFree) {
    EXPECT_TRUE(synth::isKeychainFreeLaunch(juce::StringArray{"--no-audio-device"}, {}, {}));
    EXPECT_TRUE(synth::isKeychainFreeLaunch({}, {}, "1"));
    EXPECT_TRUE(synth::isKeychainFreeLaunch(juce::StringArray{"--no-keychain"}, {}, {}));
    EXPECT_FALSE(synth::isKeychainFreeLaunch({}, {}, {}));
}

TEST(AutomationLaunchTest, KeychainPolicyEnablesInMemoryStoreForAutomationLaunch) {
    KeychainSwitchGuard guard;
    synth::KeychainTokenStore::useInMemoryStoreForProcess(false);
    EXPECT_FALSE(synth::KeychainTokenStore{}.isMemoryOnly());

    EXPECT_TRUE(synth::applyKeychainPolicyForLaunch(juce::StringArray{"--no-audio-device"}, {}, {}));
    EXPECT_TRUE(synth::KeychainTokenStore{}.isMemoryOnly());
}

TEST(AutomationLaunchTest, KeychainPolicyEnablesInMemoryStoreForNoKeychainFlag) {
    KeychainSwitchGuard guard;
    synth::KeychainTokenStore::useInMemoryStoreForProcess(false);

    EXPECT_TRUE(synth::applyKeychainPolicyForLaunch(juce::StringArray{"--no-keychain"}, {}, {}));
    EXPECT_TRUE(synth::KeychainTokenStore{}.isMemoryOnly());
}

TEST(AutomationLaunchTest, KeychainPolicyLeavesNormalLaunchOnThePlatformStore) {
    KeychainSwitchGuard guard;
    synth::KeychainTokenStore::useInMemoryStoreForProcess(false);

    EXPECT_FALSE(synth::applyKeychainPolicyForLaunch({}, {}, {}));
    EXPECT_FALSE(synth::KeychainTokenStore{}.isMemoryOnly());
}
