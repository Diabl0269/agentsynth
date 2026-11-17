#pragma once

#include "TestMode/TestModeCommands.h"

// TestModeAudioPump.h: `audioPump` (docs/development/test-mode.md#audio-pump). A test-mode launch renders through a
// silent audio device on a real audio thread, so there is nothing to start. `audioPump {run: true}` makes sure the
// device callback is attached (feedVisual detaches it) and reports the device's block counter; `{run: false}` changes
// nothing; `{note, down}` delivers a note through the seam real MIDI input uses.
namespace synth::testmode {

/** Adds audioPump. */
void registerAudioPumpCommands(CommandTable& table);

} // namespace synth::testmode
