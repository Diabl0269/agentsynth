// AudioDeviceSectionTests.cpp: the Audio tab's Device section picking an output device, against a fake device type
// whose second device has a different default buffer size from the first.
#include "../../FakeAudioIODevice.h"
#include "UI/Settings/AudioSettingsTab/AudioDeviceSection.h"
#include <gtest/gtest.h>

namespace {
constexpr int kPodsDefaultBuffer = 256;

// Offers 256 and 512 samples but defaults to 256, as a Bluetooth output might.
class PodsDevice : public synth::test::FakeAudioIODevice {
public:
    PodsDevice()
        : FakeAudioIODevice(0, 2, 0, 0, synth::test::kFakeDeviceSampleRate, kPodsDefaultBuffer) {}

    juce::Array<int> getAvailableBufferSizes() override { return {kPodsDefaultBuffer, 512}; }

    // Reports the size it was opened with, as a real device does.
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int bufferSize) override {
        opened_ = bufferSize;
        return {};
    }
    int getCurrentBufferSizeSamples() override { return opened_; }

private:
    int opened_ = kPodsDefaultBuffer;
};

class TwoDeviceType : public juce::AudioIODeviceType {
public:
    TwoDeviceType()
        : AudioIODeviceType("Fake") {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {"Speakers", "Pods"}; }
    int getDefaultDeviceIndex(bool) const override { return 0; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return 0; }
    bool hasSeparateInputsAndOutputs() const override { return true; }
    juce::AudioIODevice* createDevice(const juce::String& outputName, const juce::String&) override {
        if (outputName == "Pods")
            return new PodsDevice();
        if (outputName == "Speakers")
            return new synth::test::FakeAudioIODevice(0, 2);
        return nullptr;
    }
};
} // namespace

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
