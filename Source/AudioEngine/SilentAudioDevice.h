#pragma once

#include <atomic>
#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_core/juce_core.h>

// A device type with one output-only device that renders on its own thread at the wall clock's pace and throws the
// samples away. An automation launch (and a test that wants a live audio thread) adds it to an AudioDeviceManager
// before initialise(), so the platform's device types are never created and no hardware is touched.
namespace synth {

class SilentAudioIODevice
    : public juce::AudioIODevice
    , private juce::Thread {
public:
    static constexpr const char* kTypeName = "Silent";
    static constexpr const char* kDeviceName = "Silent test output";
    static constexpr int kMaxOutputLatency = 96000;

    /** `outputLatencySamples` is what getOutputLatencyInSamples() reports (clamped to 0..kMaxOutputLatency). */
    explicit SilentAudioIODevice(int outputLatencySamples = 0);
    ~SilentAudioIODevice() override;

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;

    juce::String open(const juce::BigInteger& inputChannels, const juce::BigInteger& outputChannels, double sampleRate,
                      int bufferSizeSamples) override;
    void close() override;
    bool isOpen() override;

    /** Message thread. Calls audioDeviceAboutToStart, then renders blocks on the device thread. */
    void start(juce::AudioIODeviceCallback* callback) override;
    /** Message thread. Returns only once the device thread is out of the callback; then audioDeviceStopped. */
    void stop() override;
    bool isPlaying() override;

    juce::String getLastError() override;
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override;
    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

    /** Any thread. Like a driver's number, the engine reads it at the next start (audioDeviceAboutToStart). */
    void setOutputLatency(int samples) noexcept;

    /** Any thread. Blocks handed to the callback since construction. */
    std::int64_t blocksRendered() const noexcept { return blocks_.load(std::memory_order_relaxed); }

private:
    void run() override;

    double sampleRate_ = 44100.0;
    int blockSize_ = 512;
    std::atomic<int> outputLatency_{0};
    std::atomic<bool> open_{false};
    std::atomic<bool> playing_{false};
    juce::CriticalSection callbackLock_;
    juce::AudioIODeviceCallback* callback_ = nullptr;
    std::atomic<std::int64_t> blocks_{0};
};

class SilentAudioDeviceType : public juce::AudioIODeviceType {
public:
    SilentAudioDeviceType();

    /** Any thread. The output latency devices created from now on report. */
    void setOutputLatency(int samples) noexcept;

    void scanForDevices() override;
    juce::StringArray getDeviceNames(bool wantInputNames = false) const override;
    int getDefaultDeviceIndex(bool forInput) const override;
    int getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const override;
    bool hasSeparateInputsAndOutputs() const override;
    juce::AudioIODevice* createDevice(const juce::String& outputDeviceName,
                                      const juce::String& inputDeviceName) override;

private:
    std::atomic<int> outputLatency_{0};
};

} // namespace synth
