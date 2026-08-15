#pragma once

// The repo's one fake audio device (TL6-1), shared so there is never a second one.
//
// It is the pattern for driving AudioEngine's juce::AudioIODeviceCallback half headlessly: it
// implements juce::AudioIODevice with fixed, made-up numbers and never starts a thread, so a test
// calls audioDeviceAboutToStart() and audioDeviceIOCallbackWithContext() BY HAND, in the same order
// a real device would, with synthetic input arrays it can then assert against. `start()`
// deliberately ignores the callback it is given, so there is exactly one thread (the test's) and
// every block is where the test put it.
//
// Extend this rather than writing a second fake: everything about it is settable through the
// constructor. Users: Tests/AudioInputTests.cpp (TL6-1), Tests/AudioInputModuleTests.cpp (TL6-2).

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::test {

inline constexpr double kFakeDeviceSampleRate = 48000.0;
inline constexpr int kFakeDeviceBlockSize = 512;
inline constexpr int kFakeDeviceInputLatency = 64;
inline constexpr int kFakeDeviceOutputLatency = 128;

class FakeAudioIODevice : public juce::AudioIODevice {
public:
    FakeAudioIODevice(int numInputChannels, int numOutputChannels)
        : juce::AudioIODevice("Fake", "Test") {
        if (numInputChannels > 0)
            activeInputs.setRange(0, numInputChannels, true);
        if (numOutputChannels > 0)
            activeOutputs.setRange(0, numOutputChannels, true);
    }

    juce::StringArray getOutputChannelNames() override { return channelNames("Out", activeOutputs); }
    juce::StringArray getInputChannelNames() override { return channelNames("In", activeInputs); }

    juce::Array<double> getAvailableSampleRates() override { return {kFakeDeviceSampleRate}; }
    juce::Array<int> getAvailableBufferSizes() override { return {kFakeDeviceBlockSize}; }
    int getDefaultBufferSize() override { return kFakeDeviceBlockSize; }

    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override { return {}; }
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return false; }
    juce::String getLastError() override { return {}; }

    int getCurrentBufferSizeSamples() override { return kFakeDeviceBlockSize; }
    double getCurrentSampleRate() override { return kFakeDeviceSampleRate; }
    int getCurrentBitDepth() override { return 32; }

    juce::BigInteger getActiveOutputChannels() const override { return activeOutputs; }
    juce::BigInteger getActiveInputChannels() const override { return activeInputs; }

    int getOutputLatencyInSamples() override { return kFakeDeviceOutputLatency; }
    int getInputLatencyInSamples() override { return kFakeDeviceInputLatency; }

private:
    static juce::StringArray channelNames(const juce::String& prefix, const juce::BigInteger& active) {
        juce::StringArray names;
        for (int i = 0; i < active.countNumberOfSetBits(); ++i)
            names.add(prefix + " " + juce::String(i + 1));
        return names;
    }

    juce::BigInteger activeInputs, activeOutputs;
};

} // namespace synth::test
