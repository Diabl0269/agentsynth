#include "DeviceInputEnable.h"

namespace synth {

// An interface that plays AND records (the usual case) keeps one device for both directions, which avoids
// JUCE's two-device combiner and its drift; only a device with no inputs at all falls back to the default.
juce::String chooseInputDeviceToEnable(const juce::String& currentInput, const juce::String& outputDevice,
                                       const juce::StringArray& inputDeviceNames,
                                       const juce::String& defaultInputDevice) {
    if (currentInput.isNotEmpty())
        return currentInput;
    if (outputDevice.isNotEmpty() && inputDeviceNames.contains(outputDevice))
        return outputDevice;
    return defaultInputDevice;
}

// Explicit channels (useDefaultInputChannels false) are what JUCE persists as "audioDeviceInChans", so the
// choice survives a relaunch through the engine's ordinary device-state save.
juce::AudioDeviceManager::AudioDeviceSetup makeInputEnabledSetup(juce::AudioDeviceManager::AudioDeviceSetup current,
                                                                 const juce::String& inputDevice, int deviceInputs) {
    current.inputDeviceName = inputDevice;
    current.useDefaultInputChannels = false;
    current.inputChannels.clear();
    const int count = juce::jlimit(0, kMaxEnabledDeviceInputs, deviceInputs);
    if (count > 0)
        current.inputChannels.setRange(0, count, true);
    return current;
}

juce::AudioDeviceManager::AudioDeviceSetup
makeInputJacksToggledSetup(juce::AudioDeviceManager::AudioDeviceSetup current, const juce::String& inputDevice,
                           std::uint32_t activeMask, int firstJack, int numJacks, bool on) {
    current.inputDeviceName = inputDevice;
    current.useDefaultInputChannels = false;
    current.inputChannels.clear();
    for (int channel = 0; channel < kMaxEnabledDeviceInputs; ++channel)
        if (channel >= firstJack && channel < firstJack + numJacks ? on
                                                                   : (activeMask & (std::uint32_t{1} << channel)) != 0)
            current.inputChannels.setBit(channel);
    return current;
}

} // namespace synth
