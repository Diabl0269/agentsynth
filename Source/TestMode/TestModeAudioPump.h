#pragma once

#include "TestMode/TestModeCommands.h"

// TestModeAudioPump.h: `audioPump` (docs/development/test-mode.md#audio-pump). A test-mode launch opens no audio
// device, so nothing renders the graph and no display that shows what the sound does (a modulated wavetable frame, a
// filter curve under an LFO) ever moves. `audioPump {run: true}` renders the open project in real time on the message
// thread, the way a bounce does, until `audioPump {run: false}`; nothing is heard.
namespace synth::testmode {

/** Adds audioPump. */
void registerAudioPumpCommands(CommandTable& table);

} // namespace synth::testmode
