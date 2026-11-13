#pragma once

#include <cstdint>
#include <juce_audio_devices/juce_audio_devices.h>

// The pure half of AudioEngine::enableDeviceInputs(): which device's inputs to turn on and the device setup that
// does it, kept free of a live juce::AudioDeviceManager so a test can pin both.

namespace synth {

// Most input channels "turn on inputs" enables (the audio-input model's channel range).
inline constexpr int kMaxEnabledDeviceInputs = 8;

// `currentInput` when set; else `outputDevice` when it is in `inputDeviceNames`; else `defaultInputDevice`.
juce::String chooseInputDeviceToEnable(const juce::String& currentInput, const juce::String& outputDevice,
                                       const juce::StringArray& inputDeviceNames,
                                       const juce::String& defaultInputDevice);

// `current` with `inputDevice` selected and its first min(deviceInputs, kMaxEnabledDeviceInputs) inputs on.
juce::AudioDeviceManager::AudioDeviceSetup makeInputEnabledSetup(juce::AudioDeviceManager::AudioDeviceSetup current,
                                                                 const juce::String& inputDevice, int deviceInputs);

// `current` with `inputDevice` selected and exactly the inputs of `activeMask` (bit c = 0-based input c) on, except
// the `numJacks` jacks from `firstJack`, which are set to `on`. For turning a jack (or a stereo pair) on or off while
// leaving the others as they are.
juce::AudioDeviceManager::AudioDeviceSetup
makeInputJacksToggledSetup(juce::AudioDeviceManager::AudioDeviceSetup current, const juce::String& inputDevice,
                           std::uint32_t activeMask, int firstJack, int numJacks, bool on);

} // namespace synth
