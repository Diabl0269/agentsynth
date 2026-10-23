// Concern: recording straight from the input -- the input-take push, the input channel map and level peaks, and
// turning a device's inputs on.

#include "AudioEngine.h"
#include "DeviceInputEnable.h"

// Called from both prepare paths. JUCE hands the callback only the ACTIVE inputs, packed in ascending order, so
// device input c sits at snapshot index "active inputs below c" -- with inputs 3 and 4 on alone, they arrive as
// snapshot 0 and 1. A take names device inputs; this is where they are translated.
void AudioEngine::mapActiveInputs(const juce::BigInteger& activeInputs) noexcept {
    int packed = 0;
    for (int channel = 0; channel < (int)inputChannelToSnapshot_.size(); ++channel) {
        const bool on = activeInputs[channel];
        inputChannelToSnapshot_[(std::size_t)channel].store(on ? packed : -1, std::memory_order_relaxed);
        if (on)
            ++packed;
    }
}

bool AudioEngine::isInputChannelActive(int channel) const noexcept {
    if (channel < 0 || channel >= (int)inputChannelToSnapshot_.size())
        return false;
    const int packed = inputChannelToSnapshot_[(std::size_t)channel].load(std::memory_order_relaxed);
    return packed >= 0 && packed < std::min(deviceInputChannelCount_.load(std::memory_order_relaxed),
                                            synth::TransportService::kMaxDeviceInputChannels);
}

// AUDIO THREAD, once per render pass, right after the snapshot is published. A pass with no usable input still
// pushes (as silence) so every take keeps one frame per transport sample from its anchor on.
void AudioEngine::pushInputTakes(int sampleOffset, int numSamples) noexcept {
    if (!inputRecorder_.isAnyCapturing() || numSamples <= 0)
        return;
    const int offset = std::max(0, sampleOffset);
    const bool haveInput =
        deviceInputChannelsThisBlock_ > 0 && offset + numSamples <= deviceInputSnapshot_.getNumSamples();
    for (std::size_t channel = 0; channel < inputTakePointers_.size(); ++channel) {
        const int packed = inputChannelToSnapshot_[channel].load(std::memory_order_relaxed);
        inputTakePointers_[channel] = haveInput && packed >= 0 && packed < deviceInputChannelsThisBlock_
                                          ? deviceInputSnapshot_.getReadPointer(packed) + offset
                                          : nullptr;
    }
    inputRecorder_.push(inputTakePointers_.data(), (int)inputTakePointers_.size(), numSamples,
                        transport.getCurrentBlockInfo().blockStartSample, /*anchorValid*/ true);
}

// AUDIO THREAD, from captureDeviceInput. Keeps the loudest block peak per snapshot channel until the message
// thread reads (and resets) it; the compare-exchange only ever raises the stored value.
void AudioEngine::storeInputPeaks(int channels, int numSamples) noexcept {
    for (int channel = 0; channel < channels && channel < (int)inputPeaks_.size(); ++channel) {
        const float peak = deviceInputSnapshot_.getMagnitude(channel, 0, numSamples);
        auto& slot = inputPeaks_[(std::size_t)channel];
        float previous = slot.load(std::memory_order_relaxed);
        while (peak > previous && !slot.compare_exchange_weak(previous, peak, std::memory_order_relaxed)) {
        }
    }
}

float AudioEngine::readInputPeak(int channel) noexcept {
    if (!isInputChannelActive(channel))
        return 0.0f;
    const int packed = inputChannelToSnapshot_[(std::size_t)channel].load(std::memory_order_relaxed);
    return inputPeaks_[(std::size_t)packed].exchange(0.0f, std::memory_order_relaxed);
}

// The running device answers for itself; any other device is created (not opened) just to count its inputs, once
// per name, since that is a driver query the side pane may ask for often.
int AudioEngine::countDeviceInputs(const juce::String& deviceName) {
    if (deviceName.isEmpty())
        return 0;
    // A device opened for output only reports no input names, so a zero here still falls through to the probe.
    if (auto* device = deviceManager.getCurrentAudioDevice(); device != nullptr && device->getName() == deviceName)
        if (const int count = device->getInputChannelNames().size(); count > 0)
            return count;
    if (const auto cached = deviceInputCountCache_.find(deviceName); cached != deviceInputCountCache_.end())
        return cached->second;
    int count = 0;
    if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
        const std::unique_ptr<juce::AudioIODevice> probe(type->createDevice({}, deviceName));
        if (probe != nullptr)
            count = probe->getInputChannelNames().size();
    }
    deviceInputCountCache_[deviceName] = count;
    return count;
}

AudioEngine::AudioInputInfo AudioEngine::getAudioInputInfo() {
    AudioInputInfo info;
    info.hosted = isHosted();
    info.activeInputs = std::min(deviceInputChannelCount_.load(std::memory_order_relaxed),
                                 synth::TransportService::kMaxDeviceInputChannels);
    for (int channel = 0; channel < (int)inputChannelToSnapshot_.size(); ++channel)
        if (isInputChannelActive(channel))
            info.activeMask |= std::uint32_t{1} << channel;
    if (info.hosted) {
        info.deviceInputs = info.activeInputs;
        return info;
    }
    const auto setup = deviceManager.getAudioDeviceSetup();
    info.deviceName = setup.inputDeviceName;
    if (info.deviceName.isEmpty()) {
        if (auto* type = deviceManager.getCurrentDeviceTypeObject()) {
            const auto names = type->getDeviceNames(true);
            info.deviceName = synth::chooseInputDeviceToEnable({}, setup.outputDeviceName, names,
                                                               names[type->getDefaultDeviceIndex(true)]);
        }
    }
    info.deviceInputs = std::max(countDeviceInputs(info.deviceName), info.activeInputs);
    return info;
}

// The permission prompt comes first (it is asynchronous on platforms that ask), and the setup is applied only once
// it is granted; the weak reference drops the answer if the engine is gone by then.
void AudioEngine::enableDeviceInputs() {
    if (isHosted())
        return;
    const auto permission = juce::RuntimePermissions::recordAudio;
    if (juce::RuntimePermissions::isRequired(permission) && !juce::RuntimePermissions::isGranted(permission)) {
        juce::RuntimePermissions::request(permission, [weak = juce::WeakReference<AudioEngine>(this)](bool granted) {
            if (granted && weak != nullptr)
                weak->applyInputEnabledSetup();
        });
        return;
    }
    applyInputEnabledSetup();
}

// Returns JUCE's error text, empty on success. The device restarts with the new channel mask, which re-runs
// audioDeviceAboutToStart (and so the input map and the format-change path) like any other device change.
juce::String AudioEngine::applyInputEnabledSetup() {
    auto* type = deviceManager.getCurrentDeviceTypeObject();
    if (type == nullptr)
        return "No audio device";
    const auto setup = deviceManager.getAudioDeviceSetup();
    const auto names = type->getDeviceNames(true);
    const auto inputDevice = synth::chooseInputDeviceToEnable(setup.inputDeviceName, setup.outputDeviceName, names,
                                                              names[type->getDefaultDeviceIndex(true)]);
    if (inputDevice.isEmpty())
        return "No input device";
    return deviceManager.setAudioDeviceSetup(
        synth::makeInputEnabledSetup(setup, inputDevice, countDeviceInputs(inputDevice)), true);
}
