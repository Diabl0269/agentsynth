// AudioDeviceSectionTests.cpp: the Audio tab's Device section picking an output device, against a fake device type
// whose second device has a different default buffer size from the first.
#include "../../FakeAudioIODevice.h"
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"
#include <gtest/gtest.h>

namespace {
constexpr int kPodsDefaultBuffer = 256;
bool gPodsRejectLargeBuffer = false;

// Offers 256 and 512 samples but defaults to 256, as a Bluetooth output might.
class PodsDevice : public synth::test::FakeAudioIODevice {
public:
    PodsDevice()
        : FakeAudioIODevice(0, 2, 0, 0, synth::test::kFakeDeviceSampleRate, kPodsDefaultBuffer) {}

    juce::Array<int> getAvailableBufferSizes() override { return {kPodsDefaultBuffer, 512}; }

    // Reports the size it was opened with, as a real device does.
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int bufferSize) override {
        if (gPodsRejectLargeBuffer && bufferSize > kPodsDefaultBuffer)
            return "buffer too large";
        opened_ = bufferSize;
        return {};
    }
    int getCurrentBufferSizeSamples() override { return opened_; }

private:
    int opened_ = kPodsDefaultBuffer;
};

// A device that never opens.
class BrokenDevice : public synth::test::FakeAudioIODevice {
public:
    BrokenDevice()
        : FakeAudioIODevice(0, 2) {}
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return "boom"; }
};

class TwoDeviceType : public juce::AudioIODeviceType {
public:
    TwoDeviceType()
        : AudioIODeviceType("Fake") {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Speakers", "Pods", "Broken"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& outputName, const juce::String&) override {
        if (outputName == "Pods")
            return new PodsDevice();
        if (outputName == "Broken")
            return new BrokenDevice();
        if (outputName == "Speakers")
            return new synth::test::FakeAudioIODevice(0, 2);
        return nullptr;
    }
};
} // namespace

namespace {
struct Rig {
    Rig() {
        manager.addAudioDeviceType(std::make_unique<TwoDeviceType>());
        manager.setCurrentAudioDeviceType("Fake", true);
        auto setup = manager.getAudioDeviceSetup();
        setup.outputDeviceName = "Speakers";
        setup.sampleRate = synth::test::kFakeDeviceSampleRate;
        setup.bufferSize = synth::test::kFakeDeviceBlockSize;
        EXPECT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty());
        gPodsRejectLargeBuffer = false;
        // Built last: constructing it earlier makes the manager create the machine's real device types first.
        section = std::make_unique<AudioDeviceSection>(manager, source);
        section->setSize(520, 300);
    }
    ~Rig() { gPodsRejectLargeBuffer = false; }
    void pick(int id) { section->getOutputBoxForTest().setSelectedId(id, juce::sendNotificationSync); }

    juce::AudioDeviceManager manager;
    DeviceManagerSettingsSource source{manager};
    std::unique_ptr<AudioDeviceSection> section;
};
} // namespace

// A device picked before comes back with what the user last had on it.
TEST(AudioDeviceSectionTest, APickedDeviceComesBackWithItsRememberedBufferAndChannels) {
    Rig rig;
    // The fake device names its own type, so the key is whatever the manager reports once a device is open.
    rig.source.layoutStore().setDeviceSettings(rig.manager.getCurrentAudioDeviceType(), "Pods",
                                               {synth::test::kFakeDeviceSampleRate, 512, 0b11});
    rig.pick(2);
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().bufferSize, 512);
}

// Remembered values the device no longer accepts fall back to its own defaults instead of failing the pick.
TEST(AudioDeviceSectionTest, RememberedValuesThatNoLongerOpenFallBackToTheDeviceDefaults) {
    Rig rig;
    // The fake device names its own type, so the key is whatever the manager reports once a device is open.
    rig.source.layoutStore().setDeviceSettings(rig.manager.getCurrentAudioDeviceType(), "Pods",
                                               {synth::test::kFakeDeviceSampleRate, 512, 0b11});
    gPodsRejectLargeBuffer = true;
    rig.pick(2);
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().bufferSize, kPodsDefaultBuffer);
}

// A device that will not open leaves the previous one playing, and the box still names it.
TEST(AudioDeviceSectionTest, ADeviceThatWillNotOpenLeavesThePreviousOneOpen) {
    Rig rig;
    juce::String failedDevice, failedError;
    rig.section->onOpenFailed = [&](const juce::String& name, const juce::String& error) {
        failedDevice = name;
        failedError = error;
    };
    rig.pick(3);
    EXPECT_EQ(failedDevice, "Broken");
    EXPECT_EQ(failedError, "boom");
    ASSERT_NE(rig.manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(rig.manager.getAudioDeviceSetup().outputDeviceName, "Speakers");
    EXPECT_EQ(rig.section->getOutputBoxForTest().getText(), "Speakers");
}

// Picking an output device opens it with its own default buffer size instead of carrying over the previous
// device's, and the box then shows the device that really opened.
TEST(AudioDeviceSectionTest, PickingAnOutputOpensItAtItsOwnDefaultBufferSize) {
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType(std::make_unique<TwoDeviceType>());
    manager.setCurrentAudioDeviceType("Fake", true);
    auto setup = manager.getAudioDeviceSetup();
    setup.outputDeviceName = "Speakers";
    setup.sampleRate = synth::test::kFakeDeviceSampleRate;
    setup.bufferSize = synth::test::kFakeDeviceBlockSize;
    ASSERT_TRUE(manager.setAudioDeviceSetup(setup, true).isEmpty());

    DeviceManagerSettingsSource source(manager);
    AudioDeviceSection section(manager, source);
    section.setSize(520, 300);

    auto& output = section.getOutputBoxForTest();
    output.setSelectedId(2, juce::sendNotificationSync);

    ASSERT_NE(manager.getCurrentAudioDevice(), nullptr);
    EXPECT_EQ(manager.getAudioDeviceSetup().outputDeviceName, "Pods");
    EXPECT_EQ(manager.getAudioDeviceSetup().bufferSize, kPodsDefaultBuffer);
    EXPECT_EQ(output.getText(), "Pods");
}
