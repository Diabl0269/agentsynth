#pragma once

#include <juce_core/juce_core.h>

// FRO27: a saved juce::AudioDeviceManager state must never name an input device unless input
// channels are actually enabled in that same state -- see DeviceStateInputs.cpp for the JUCE
// behaviour this works around (updateXml() always writes an input device name, even for an
// output-only setup). Shared by AudioEngine (device-state save/load) and MainComponent (the
// mic-permission gate), so it lives in Core rather than beside either caller.

namespace synth {

// True iff `deviceSetup` (a juce::AudioDeviceManager "DEVICESETUP" element, e.g. from
// createStateXml() or a saved/loaded settings string) declares at least one enabled input
// channel via its "audioDeviceInChans" bit-string attribute ("11", "01", ...). False when the
// attribute is absent (JUCE's useDefaultInputChannels path, never load-bearing here since every
// caller of this file requests 0 input channels) or contains no '1'.
bool deviceStateEnablesInput(const juce::XmlElement& deviceSetup);

// Removes any input-device name from `deviceSetup` when deviceStateEnablesInput() is false for
// it, so a state that never actually enabled input can never open one. Leaves an input-enabling
// state completely untouched. See the .cpp definition for the legacy-attribute handling.
void stripUnusedInputDevice(juce::XmlElement& deviceSetup);

} // namespace synth
