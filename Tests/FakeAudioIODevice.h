#pragma once

// The repo's one fake audio device, shared so there is never a second one.
//
// It is the pattern for driving AudioEngine's juce::AudioIODeviceCallback half headlessly: it
// implements juce::AudioIODevice with fixed, made-up numbers and never starts a thread, so a test
// calls audioDeviceAboutToStart() and audioDeviceIOCallbackWithContext() BY HAND, in the same order
// a real device would, with synthetic input arrays it can then assert against. `start()`
// deliberately ignores the callback it is given, so there is exactly one thread (the test's) and
// every block is where the test put it.
//
// Extend this rather than writing a second fake: everything about it is settable through the
// constructor. Users: Tests/AudioInputTests.cpp, Tests/AudioInputModuleTests.cpp.

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::test {

inline constexpr double kFakeDeviceSampleRate = 48000.0;
inline constexpr int kFakeDeviceBlockSize = 512;
inline constexpr int kFakeDeviceInputLatency = 64;
inline constexpr int kFakeDeviceOutputLatency = 128;

class FakeAudioIODevice : public juce::AudioIODevice {
public:
    /** @param inputLatency  what getInputLatencyInSamples() reports — the default is the shared
     *         kFakeDeviceInputLatency every existing user already asserts against.
     *  @param outputLatency ditto for getOutputLatencyInSamples(). The alignment tests pass
     *         0/0 to prove "zero-latency devices shift nothing", so both are settable rather than
     *         fixed constants.
     *  @param sampleRate what getCurrentSampleRate() reports — the device-change tests construct
     *         a SECOND fake at a different rate to simulate a mid-session device switch (the same
     *         instance can't change rate: a real device restart is exactly "stop this one, start a
     *         different juce::AudioIODevice", which is what audioDeviceAboutToStart(&anotherFake)
     *         models). Defaults to kFakeDeviceSampleRate so every existing caller is unaffected.
     *  @param blockSize   ditto for getCurrentBufferSizeSamples() / getDefaultBufferSize(). */
    FakeAudioIODevice(int numInputChannels, int numOutputChannels, int inputLatency = kFakeDeviceInputLatency,
                      int outputLatency = kFakeDeviceOutputLatency, double sampleRate = kFakeDeviceSampleRate,
                      int blockSize = kFakeDeviceBlockSize)
        : juce::AudioIODevice("Fake", "Test")
        , inputLatency_(inputLatency)
        , outputLatency_(outputLatency)
        , sampleRate_(sampleRate)
        , blockSize_(blockSize) {
        if (numInputChannels > 0)
            activeInputs.setRange(0, numInputChannels, true);
        if (numOutputChannels > 0)
            activeOutputs.setRange(0, numOutputChannels, true);
    }

    juce::StringArray getOutputChannelNames() override { return channelNames("Out", activeOutputs); }
    juce::StringArray getInputChannelNames() override { return channelNames("In", activeInputs); }

    juce::Array<double> getAvailableSampleRates() override { return {sampleRate_}; }
    juce::Array<int> getAvailableBufferSizes() override { return {blockSize_}; }
    int getDefaultBufferSize() override { return blockSize_; }

    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return false; }
    juce::String getLastError() override { return {}; }

    int getCurrentBufferSizeSamples() override { return blockSize_; }
    double getCurrentSampleRate() override { return sampleRate_; }
    int getCurrentBitDepth() override { return 32; }

    juce::BigInteger getActiveOutputChannels() const override { return activeOutputs; }
    juce::BigInteger getActiveInputChannels() const override { return activeInputs; }

    int getOutputLatencyInSamples() override { return outputLatency_; }
    int getInputLatencyInSamples() override { return inputLatency_; }

private:
    static juce::StringArray channelNames(const juce::String& prefix, const juce::BigInteger& active) {
        juce::StringArray names;
        for (int i = 0; i < active.countNumberOfSetBits(); ++i)
            names.add(prefix + " " + juce::String(i + 1));
        return names;
    }

    const int inputLatency_;
    const int outputLatency_;
    const double sampleRate_;
    const int blockSize_;
    juce::BigInteger activeInputs, activeOutputs;
};

} // namespace synth::test
