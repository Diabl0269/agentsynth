#pragma once

#include "TestMode/TestModeCommands.h"

// TestModeAudioDevice.h: `audioDevice` (docs/development/test-mode.md#audio-device). Changes the silent device's sample
// rate, block size and output latency, restarts it the way JUCE does for a device that went away and came back, and
// checks that the device and the engine agree afterwards, so a bug at 48 kHz, a small buffer, with latency or after a
// restart shows up in a scenario.
namespace synth::testmode {

/** Adds audioDevice. */
void registerAudioDeviceCommands(CommandTable& table);

} // namespace synth::testmode
