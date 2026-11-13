#pragma once

#include <juce_core/juce_core.h>

// Whether the audio device called `deviceName` is a Bluetooth one (AirPods, a Bluetooth speaker), by the transport
// type CoreAudio reports. Always false off macOS, where JUCE does not expose the transport.
namespace synth::audio {
bool isBluetoothDevice(const juce::String& deviceName);
}
