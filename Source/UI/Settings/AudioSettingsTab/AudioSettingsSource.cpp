// Concern: the Audio tab's fallback source, straight from the device manager.
#include "UI/Settings/AudioSettingsTab/AudioSettingsSource.h"

#include "AudioEngine/DeviceInputEnable.h"

AudioInterfaceInfo DeviceManagerSettingsSource::currentInterface() {
    AudioInterfaceInfo info;
    info.deviceType = deviceManager_.getCurrentAudioDeviceType();
    if (auto* device = deviceManager_.getCurrentAudioDevice()) {
        const auto setup = deviceManager_.getAudioDeviceSetup();
        info.deviceName = setup.inputDeviceName.isNotEmpty() ? setup.inputDeviceName : setup.outputDeviceName;
        info.numInputs = device->getInputChannelNames().size();
        const auto active = device->getActiveInputChannels();
        for (int c = 0; c < synth::kMaxEnabledDeviceInputs; ++c)
            if (active[c])
                info.usedMask |= std::uint32_t{1} << c;
        info.sampleRate = device->getCurrentSampleRate();
        info.bufferSamples = device->getCurrentBufferSizeSamples();
        info.recordingLatencySamples = device->getInputLatencyInSamples() + device->getOutputLatencyInSamples();
    }
    return info;
}

void DeviceManagerSettingsSource::noteOpenInterface() {
    const auto info = currentInterface();
    if (info.deviceName.isNotEmpty() && !store_.has(info.deviceType, info.deviceName))
        store_.touch(info.deviceType, info.deviceName);
}

void DeviceManagerSettingsSource::setInputsUsed(int firstJack, int numJacks, bool used) {
    const auto info = currentInterface();
    if (info.deviceName.isEmpty())
        return;
    deviceManager_.setAudioDeviceSetup(synth::makeInputJacksToggledSetup(deviceManager_.getAudioDeviceSetup(),
                                                                         info.deviceName, info.usedMask, firstJack,
                                                                         numJacks, used),
                                       true);
}

std::vector<AudioMidiDevice> DeviceManagerSettingsSource::midiInputs() {
    std::vector<AudioMidiDevice> devices;
    for (const auto& device : juce::MidiInput::getAvailableDevices())
        devices.push_back({device.identifier, device.name, deviceManager_.isMidiInputDeviceEnabled(device.identifier)});
    return devices;
}

void DeviceManagerSettingsSource::setMidiInputEnabled(const juce::String& identifier, bool enabled) {
    deviceManager_.setMidiInputDeviceEnabled(identifier, enabled);
}

std::vector<AudioMidiDevice> DeviceManagerSettingsSource::midiOutputs() {
    std::vector<AudioMidiDevice> devices;
    const auto chosen = deviceManager_.getDefaultMidiOutputIdentifier();
    for (const auto& device : juce::MidiOutput::getAvailableDevices())
        devices.push_back({device.identifier, device.name, device.identifier == chosen});
    return devices;
}

void DeviceManagerSettingsSource::setMidiOutput(const juce::String& identifier) {
    deviceManager_.setDefaultMidiOutputDevice(identifier);
}
