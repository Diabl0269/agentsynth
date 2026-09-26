// FRO27: synth::deviceStateEnablesInput / synth::stripUnusedInputDevice (the standalone-only fix
// for a saved device state naming an input device the user never actually enabled), plus
// AudioEngine::setSavedDeviceState / savedDeviceStateEnablesInput applying it.
//
// Headless/deterministic house rules (docs/development/test-patterns.md): no real audio device.
// The AudioEngine-level tests below drive the same SeamEngine pattern as
// Tests/Engine/AudioInputTests.cpp -- a HostMode::Standalone engine must never have initialise()
// called on it in a test without going through a subclass that overrides initialiseDevices(),
// which is the only part of initialise() that touches hardware.

#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/DeviceStateInputs.h"
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

namespace {

using synth::deviceStateEnablesInput;
using synth::stripUnusedInputDevice;

std::unique_ptr<juce::XmlElement> parse(const juce::String& xml) {
    auto element = juce::parseXML(xml);
    EXPECT_NE(element, nullptr) << "malformed test fixture XML";
    return element;
}

// ============================================================================
// deviceStateEnablesInput
// ============================================================================

TEST(DeviceStateInputsTest, NoAttributeMeansNoInput) {
    auto state = parse(R"(<DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="Speakers"/>)");
    EXPECT_FALSE(deviceStateEnablesInput(*state));
}

TEST(DeviceStateInputsTest, AllZeroChansMeansNoInput) {
    auto state = parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioDeviceInChans="00"/>)");
    EXPECT_FALSE(deviceStateEnablesInput(*state));
}

TEST(DeviceStateInputsTest, AnyOneBitMeansInputEnabled) {
    EXPECT_TRUE(deviceStateEnablesInput(*parse(R"(<DEVICESETUP audioDeviceInChans="01"/>)")));
    EXPECT_TRUE(deviceStateEnablesInput(*parse(R"(<DEVICESETUP audioDeviceInChans="1"/>)")));
    EXPECT_TRUE(deviceStateEnablesInput(*parse(R"(<DEVICESETUP audioDeviceInChans="11"/>)")));
}

// ============================================================================
// stripUnusedInputDevice
// ============================================================================

TEST(DeviceStateInputsTest, OutputOnlyStateWithNoChansAttributeLosesInputName) {
    auto state = parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioInputDeviceName="Mic"/>)");
    stripUnusedInputDevice(*state);
    EXPECT_FALSE(state->hasAttribute("audioInputDeviceName"));
    EXPECT_EQ(state->getStringAttribute("audioOutputDeviceName"), "Speakers");
}

TEST(DeviceStateInputsTest, AllZeroChansLosesInputName) {
    auto state =
        parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioInputDeviceName="Mic" audioDeviceInChans="00"/>)");
    stripUnusedInputDevice(*state);
    EXPECT_FALSE(state->hasAttribute("audioInputDeviceName"));
}

TEST(DeviceStateInputsTest, EnabledInputIsLeftCompletelyUntouched) {
    auto state =
        parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioInputDeviceName="Mic" audioDeviceInChans="11"/>)");
    const juce::String before = state->toString();
    stripUnusedInputDevice(*state);
    EXPECT_EQ(state->toString(), before);
    EXPECT_EQ(state->getStringAttribute("audioInputDeviceName"), "Mic");
}

TEST(DeviceStateInputsTest, LegacyCombinedNameBecomesOutputNameWhenNoOutputNameYet) {
    auto state = parse(R"(<DEVICESETUP audioDeviceName="Interface"/>)");
    stripUnusedInputDevice(*state);
    EXPECT_FALSE(state->hasAttribute("audioDeviceName"));
    EXPECT_FALSE(state->hasAttribute("audioInputDeviceName"));
    EXPECT_EQ(state->getStringAttribute("audioOutputDeviceName"), "Interface");
}

TEST(DeviceStateInputsTest, LegacyCombinedNameNeverOverwritesAnExistingOutputName) {
    auto state = parse(R"(<DEVICESETUP audioDeviceName="Interface" audioOutputDeviceName="Speakers"/>)");
    stripUnusedInputDevice(*state);
    EXPECT_FALSE(state->hasAttribute("audioDeviceName"));
    EXPECT_EQ(state->getStringAttribute("audioOutputDeviceName"), "Speakers");
}

// ============================================================================
// AudioEngine::setSavedDeviceState / savedDeviceStateEnablesInput
// ============================================================================

/** Same seam as Tests/Engine/AudioInputTests.cpp's SeamEngine, extended to capture the XML
 *  initialise() actually hands to initialiseDevices() -- i.e. the state as stored, after
 *  setSavedDeviceState()'s repair -- without opening a device or grabbing MIDI input. */
class SeamEngine : public AudioEngine {
public:
    using AudioEngine::AudioEngine;

    std::unique_ptr<juce::XmlElement> capturedState;

protected:
    void initialiseDevices(const juce::XmlElement* savedDeviceState) override {
        capturedState = savedDeviceState != nullptr ? std::make_unique<juce::XmlElement>(*savedDeviceState) : nullptr;
    }
};

TEST(DeviceStateInputsTest, SetSavedDeviceStateStripsAnUnusedInputName) {
    SeamEngine engine(AudioEngine::HostMode::Standalone);
    engine.setSavedDeviceState(parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioInputDeviceName="Mic"/>)"));

    EXPECT_FALSE(engine.savedDeviceStateEnablesInput());

    engine.initialise();
    ASSERT_NE(engine.capturedState, nullptr);
    EXPECT_FALSE(engine.capturedState->hasAttribute("audioInputDeviceName"));
    EXPECT_EQ(engine.capturedState->getStringAttribute("audioOutputDeviceName"), "Speakers");
    engine.shutdown();
}

TEST(DeviceStateInputsTest, SetSavedDeviceStateKeepsAnEnabledInputName) {
    SeamEngine engine(AudioEngine::HostMode::Standalone);
    engine.setSavedDeviceState(
        parse(R"(<DEVICESETUP audioOutputDeviceName="Speakers" audioInputDeviceName="Mic" audioDeviceInChans="11"/>)"));

    EXPECT_TRUE(engine.savedDeviceStateEnablesInput());

    engine.initialise();
    ASSERT_NE(engine.capturedState, nullptr);
    EXPECT_EQ(engine.capturedState->getStringAttribute("audioInputDeviceName"), "Mic");
    engine.shutdown();
}

TEST(DeviceStateInputsTest, NoSavedStateNeverEnablesInput) {
    SeamEngine engine(AudioEngine::HostMode::Standalone);
    EXPECT_FALSE(engine.savedDeviceStateEnablesInput());
    engine.initialise();
    EXPECT_EQ(engine.capturedState, nullptr);
    engine.shutdown();
}

// NOTE (per FRO27 spec): a test asserting that changeListenerCallback's own
// deviceManager.createStateXml() -> onDeviceStateChanged path strips an unused input name is not
// reachable headlessly -- createStateXml() returns null until a real device has been opened
// (Tests/Engine/AudioInputTests.cpp's DeviceStateChangeReachesTheOwnerCallback test only ever sees
// a null payload for exactly this reason). Skipped; setSavedDeviceState's repair above and
// synth::stripUnusedInputDevice's direct unit tests above cover the same logic without hardware.

} // namespace
