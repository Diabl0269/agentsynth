#pragma once

#include "AudioEngine/AudioEngine.h"

// TestModeTransportSync.h: a test-mode launch opens no audio device, so nothing drains the transport's command FIFO
// into the position snapshot the timeline reads (the loop locators, the play state). A timer on the message thread
// does it instead, only while nothing else ticks the transport (no device callbacks, no `audioPump`).
namespace synth::testmode {

/** One sync pass for `engine`: drains its transport and republishes the snapshot unless a device or the pump already
 *  ticks it. True when it synced. */
bool syncTransportWithoutAudio(AudioEngine& engine);

/** Starts the ~60 Hz timer that syncs the open window's engine. Safe to call twice. */
void startTransportSync();

} // namespace synth::testmode
