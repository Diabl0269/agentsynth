#pragma once

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth {

// An audio device type that lists no devices and opens none. A juce::AudioDeviceManager builds the
// platform's real device types only while its type list is empty, so adding this one first keeps
// every later initialise / setAudioDeviceSetup call on that manager from touching hardware.
class NoAudioDeviceType final : public juce::AudioIODeviceType {
public:
    static juce::String typeName() { return "No audio device"; }

    NoAudioDeviceType()
        : juce::AudioIODeviceType(typeName()) {}

    void scanForDevices() override {}
    juce::StringArray getDeviceNames(bool) const override { return {}; }
    int getDefaultDeviceIndex(bool) const override { return -1; }
    int getIndexOfDevice(juce::AudioIODevice*, bool) const override { return -1; }
    bool hasSeparateInputsAndOutputs() const override { return false; }
    juce::AudioIODevice* createDevice(const juce::String&, const juce::String&) override { return nullptr; }
};

} // namespace synth
