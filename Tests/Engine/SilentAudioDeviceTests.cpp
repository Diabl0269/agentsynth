// SilentAudioDevice (Source/AudioEngine/SilentAudioDevice.h): a real audio thread with no hardware behind it
// (docs/architecture/audio-engine.md#automation-launch-the-silent-device). The engine switch that renders through it is
// in SilentAudioEngineTests.cpp.

#include "../TestWait.h"
#include "AudioEngine/SilentAudioDevice.h"
#include <atomic>
#include <gtest/gtest.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include <thread>

namespace {

using synth::SilentAudioDeviceType;
using synth::SilentAudioIODevice;

constexpr double kSilentRate = 44100.0;
constexpr int kSilentBlock = 512;

// Counts what a device does to its callback, and flags any call that arrives while the device says it is stopped.
class CountingCallback : public juce::AudioIODeviceCallback {
public:
    void audioDeviceAboutToStart(juce::AudioIODevice*) override {
        ++started;
        stopped = false;
    }
    void audioDeviceStopped() override {
        ++stoppedCalls;
        stopped = true;
    }
    void audioDeviceIOCallbackWithContext(const float* const*, int, float* const* outs, int numOuts, int numSamples,
                                          const juce::AudioIODeviceCallbackContext&) override {
        if (stopped)
            callAfterStop = true;
        if (std::this_thread::get_id() == testThread)
            onTestThread = true;
        for (int ch = 0; ch < numOuts; ++ch)
            if (outs[ch] != nullptr)
                for (int i = 0; i < numSamples; ++i)
                    if (outs[ch][i] != 0.0f)
                        outputNotCleared = true;
        ++blocks;
    }

    const std::thread::id testThread = std::this_thread::get_id();
    std::atomic<int> blocks{0};
    std::atomic<bool> stopped{true};
    std::atomic<bool> callAfterStop{false};
    std::atomic<bool> onTestThread{false};
    std::atomic<bool> outputNotCleared{false};
    int started = 0;
    int stoppedCalls = 0;
};

juce::BigInteger stereo() {
    juce::BigInteger channels;
    channels.setRange(0, 2, true);
    return channels;
}

} // namespace

TEST(SilentAudioDevice, RendersOnItsOwnThreadAtTheWallClocksPace) {
    SilentAudioIODevice device;
    ASSERT_TRUE(device.open({}, stereo(), kSilentRate, kSilentBlock).isEmpty());
    CountingCallback callback;
    const double startMs = juce::Time::getMillisecondCounterHiRes();
    device.start(&callback);

    constexpr int kWantedBlocks = 20; // about 232 ms of audio
    synth::test::waitUntil([&] { return callback.blocks.load() >= kWantedBlocks; }, 60000);
    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - startMs;
    device.stop();

    EXPECT_GE(callback.blocks.load(), kWantedBlocks);
    EXPECT_FALSE(callback.onTestThread.load()) << "the callback must run on the device's own thread";
    EXPECT_FALSE(callback.outputNotCleared.load()) << "every block starts silent";
    // One-sided on purpose: a loaded machine only ever makes the device slower than the wall clock, never faster.
    const double audioMs = 1000.0 * kSilentBlock * kWantedBlocks / kSilentRate;
    EXPECT_GT(elapsedMs, audioMs * 0.6) << "blocks must not be rendered faster than real time";
    EXPECT_EQ(device.blocksRendered(), callback.blocks.load());
}

TEST(SilentAudioDevice, NeverCallsBackAfterStopReturns) {
    SilentAudioIODevice device;
    ASSERT_TRUE(device.open({}, stereo(), kSilentRate, kSilentBlock).isEmpty());
    CountingCallback callback;
    device.start(&callback);
    synth::test::waitUntil([&] { return callback.blocks.load() >= 3; }, 60000);
    device.stop();

    const int blocksAtStop = callback.blocks.load();
    EXPECT_EQ(callback.stoppedCalls, 1);
    synth::test::waitUntil([&] { return callback.blocks.load() != blocksAtStop; }, 60); // a bounded watch
    EXPECT_EQ(callback.blocks.load(), blocksAtStop);
    EXPECT_FALSE(callback.callAfterStop.load());
}

TEST(SilentAudioDevice, StartingAndStoppingFiftyTimesIsClean) {
    SilentAudioIODevice device;
    ASSERT_TRUE(device.open({}, stereo(), kSilentRate, kSilentBlock).isEmpty());
    CountingCallback callback;
    for (int i = 0; i < 50; ++i) {
        device.start(&callback);
        EXPECT_TRUE(device.isPlaying());
        device.stop();
        EXPECT_FALSE(device.isPlaying());
    }
    EXPECT_EQ(callback.started, 50);
    EXPECT_EQ(callback.stoppedCalls, 50);
    EXPECT_FALSE(callback.callAfterStop.load());
}

TEST(SilentAudioDevice, DescribesAStereoOutputOnlyDevice) {
    SilentAudioIODevice device;
    ASSERT_TRUE(device.open({}, stereo(), 48000.0, 1024).isEmpty());
    EXPECT_EQ(device.getActiveOutputChannels().countNumberOfSetBits(), 2);
    EXPECT_EQ(device.getActiveInputChannels().countNumberOfSetBits(), 0);
    EXPECT_EQ(device.getCurrentSampleRate(), 48000.0);
    EXPECT_EQ(device.getCurrentBufferSizeSamples(), 1024);
    EXPECT_EQ(device.getCurrentBitDepth(), 32);
    EXPECT_EQ(device.getOutputLatencyInSamples(), 0);
    EXPECT_EQ(device.getDefaultBufferSize(), 512);
}

TEST(SilentAudioDevice, AManagerGivenTheTypeListsOnlyThatType) {
    juce::AudioDeviceManager manager;
    manager.addAudioDeviceType(std::make_unique<SilentAudioDeviceType>());
    EXPECT_TRUE(manager.initialiseWithDefaultDevices(0, 2).isEmpty());

    const auto& types = manager.getAvailableDeviceTypes();
    ASSERT_EQ(types.size(), 1) << "no platform device type may exist beside the silent one";
    EXPECT_EQ(types[0]->getTypeName(), juce::String(SilentAudioIODevice::kTypeName));
    EXPECT_EQ(types[0]->getDeviceNames(false), juce::StringArray(SilentAudioIODevice::kDeviceName));
    EXPECT_EQ(types[0]->getDeviceNames(true).size(), 0);

    auto* device = manager.getCurrentAudioDevice();
    ASSERT_NE(device, nullptr);
    EXPECT_EQ(device->getName(), juce::String(SilentAudioIODevice::kDeviceName));

    CountingCallback callback;
    manager.addAudioCallback(&callback);
    synth::test::waitUntil([&] { return callback.blocks.load() >= 2; }, 60000);
    EXPECT_GE(callback.blocks.load(), 2);
    manager.removeAudioCallback(&callback);
    manager.closeAudioDevice();
}
