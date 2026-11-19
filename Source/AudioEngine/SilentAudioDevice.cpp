// Concern: the silent audio device -- a real audio thread with no hardware behind it.

#include "AudioEngine/SilentAudioDevice.h"
#include <cmath>

namespace synth {

namespace {

constexpr int kChannels = 2;
constexpr int kDefaultBlock = 512;
constexpr double kDefaultRate = 44100.0;
constexpr double kMaxBlocksLate = 1.0; // more than one block behind: re-base the clock, never burst a backlog

double nowMs() { return juce::Time::getMillisecondCounterHiRes(); }

const double kRates[] = {44100.0, 48000.0, 88200.0, 96000.0};
const int kBlocks[] = {64, 128, 256, 512, 1024, 2048};

// The offered value closest to `wanted`; the lower one on a tie.
template <typename T, size_t N>
T nearest(const T (&offered)[N], double wanted) {
    T best = offered[0];
    for (T value : offered)
        if (std::abs((double)value - wanted) < std::abs((double)best - wanted))
            best = value;
    return best;
}

} // namespace

SilentAudioIODevice::SilentAudioIODevice(int outputLatencySamples)
    : juce::AudioIODevice(kDeviceName, kTypeName)
    , juce::Thread("Silent audio device") {
    setOutputLatency(outputLatencySamples);
}

SilentAudioIODevice::~SilentAudioIODevice() {
    stop();
    close();
}

juce::StringArray SilentAudioIODevice::getOutputChannelNames() { return {"Left", "Right"}; }
juce::StringArray SilentAudioIODevice::getInputChannelNames() { return {}; }
juce::Array<double> SilentAudioIODevice::getAvailableSampleRates() { return {44100.0, 48000.0, 88200.0, 96000.0}; }
juce::Array<int> SilentAudioIODevice::getAvailableBufferSizes() { return {64, 128, 256, 512, 1024, 2048}; }
int SilentAudioIODevice::getDefaultBufferSize() { return kDefaultBlock; }

juce::String SilentAudioIODevice::open(const juce::BigInteger&, const juce::BigInteger&, double sampleRate,
                                       int bufferSizeSamples) {
    stop();
    // The nearest offered rate and block size: a saved or requested value outside the lists still opens.
    sampleRate_ = nearest(kRates, sampleRate);
    blockSize_ = bufferSizeSamples <= 0 ? kDefaultBlock : nearest(kBlocks, bufferSizeSamples);
    open_ = true;
    return {};
}

void SilentAudioIODevice::close() {
    stop();
    open_ = false;
}

bool SilentAudioIODevice::isOpen() { return open_.load(); }

void SilentAudioIODevice::start(juce::AudioIODeviceCallback* callback) {
    if (!open_.load() || callback == nullptr)
        return;
    stop();
    callback->audioDeviceAboutToStart(this);
    {
        const juce::ScopedLock lock(callbackLock_);
        callback_ = callback;
    }
    playing_ = true;
    startThread(Priority::high);
}

void SilentAudioIODevice::stop() {
    if (!playing_.load())
        return;
    // The thread is gone before the callback is told, so nothing renders after stop() returns. No timeout: a timed
    // stop kills a thread that is still inside the callback (holding callbackLock_) on a stalled machine, and the
    // loop always leaves within one block.
    stopThread(-1);
    juce::AudioIODeviceCallback* last = nullptr;
    {
        const juce::ScopedLock lock(callbackLock_);
        last = callback_;
        callback_ = nullptr;
    }
    playing_ = false;
    if (last != nullptr)
        last->audioDeviceStopped();
}

bool SilentAudioIODevice::isPlaying() { return playing_.load(); }
juce::String SilentAudioIODevice::getLastError() { return {}; }
int SilentAudioIODevice::getCurrentBufferSizeSamples() { return blockSize_; }
double SilentAudioIODevice::getCurrentSampleRate() { return sampleRate_; }
int SilentAudioIODevice::getCurrentBitDepth() { return 32; }

juce::BigInteger SilentAudioIODevice::getActiveOutputChannels() const {
    juce::BigInteger channels;
    channels.setRange(0, kChannels, true);
    return channels;
}

juce::BigInteger SilentAudioIODevice::getActiveInputChannels() const { return {}; }
int SilentAudioIODevice::getOutputLatencyInSamples() { return outputLatency_.load(std::memory_order_relaxed); }
void SilentAudioIODevice::setOutputLatency(int samples) noexcept {
    outputLatency_.store(juce::jlimit(0, kMaxOutputLatency, samples), std::memory_order_relaxed);
}
int SilentAudioIODevice::getInputLatencyInSamples() { return 0; }

void SilentAudioIODevice::run() {
    const double blockMs = 1000.0 * blockSize_ / sampleRate_;
    double deadline = nowMs() + blockMs;
    // Owned by this run of the thread, so a restart never shares a buffer with the thread before it.
    juce::AudioBuffer<float> buffer(kChannels, blockSize_);
    float* outs[kChannels] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};

    while (!threadShouldExit()) {
        buffer.clear();
        {
            const juce::ScopedLock lock(callbackLock_);
            if (callback_ != nullptr)
                callback_->audioDeviceIOCallbackWithContext(nullptr, 0, outs, kChannels, blockSize_, {});
        }
        blocks_.fetch_add(1, std::memory_order_relaxed);

        deadline += blockMs;
        const double remaining = deadline - nowMs();
        if (remaining < -blockMs * kMaxBlocksLate)
            deadline = nowMs() + blockMs;
        else if (remaining > 0.0)
            wait(remaining); // notify() from stopThread ends the wait early
    }
    // A run that was told to stop before its first block never took the lock; taking it once on the way out
    // orders every read this run made before stop() takes it, so the device's memory can be reused after stop().
    const juce::ScopedLock exitLock(callbackLock_);
}

SilentAudioDeviceType::SilentAudioDeviceType()
    : juce::AudioIODeviceType(SilentAudioIODevice::kTypeName) {}

void SilentAudioDeviceType::setOutputLatency(int samples) noexcept {
    outputLatency_.store(juce::jlimit(0, SilentAudioIODevice::kMaxOutputLatency, samples), std::memory_order_relaxed);
}

void SilentAudioDeviceType::scanForDevices() {}

juce::StringArray SilentAudioDeviceType::getDeviceNames(bool wantInputNames) const {
    return wantInputNames ? juce::StringArray() : juce::StringArray(SilentAudioIODevice::kDeviceName);
}

int SilentAudioDeviceType::getDefaultDeviceIndex(bool forInput) const { return forInput ? -1 : 0; }

int SilentAudioDeviceType::getIndexOfDevice(juce::AudioIODevice* device, bool asInput) const {
    return !asInput && device != nullptr && device->getName() == SilentAudioIODevice::kDeviceName ? 0 : -1;
}

bool SilentAudioDeviceType::hasSeparateInputsAndOutputs() const { return false; }

juce::AudioIODevice* SilentAudioDeviceType::createDevice(const juce::String& outputDeviceName,
                                                         const juce::String& inputDeviceName) {
    if (inputDeviceName.isNotEmpty() ||
        (outputDeviceName.isNotEmpty() && outputDeviceName != SilentAudioIODevice::kDeviceName))
        return nullptr;
    return new SilentAudioIODevice(outputLatency_.load(std::memory_order_relaxed));
}

} // namespace synth
