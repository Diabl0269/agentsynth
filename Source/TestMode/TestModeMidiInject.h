#pragma once

#include "TestMode/TestModeCommands.h"

// TestModeMidiInject.h: `midiInject` (docs/development/test-mode.md#midi-inject). A test-mode launch opens no MIDI
// device, so nothing a controller would send ever arrives. `midiInject {source, bytes}` hands one raw message to the
// engine's MIDI Remote sink as if the input with that identifier had sent it, on the same seam a real input uses.
namespace synth::testmode {

/** Adds midiInject. */
void registerMidiInjectCommands(CommandTable& table);

} // namespace synth::testmode
