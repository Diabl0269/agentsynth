#pragma once

// FRO236 (docs/control/midi-remote.md#continuous-targets): the one Core helper every surface that
// shows a Target::Continuous by name shares -- the picker, the panel's cell/inspector labels, and
// the (currently rendered-disabled) Inspector Relearn row -- so "Tempo (BPM)" / "Playhead Position" /
// "Master Volume" is spelled exactly once. Kept out of RemoteModel.h (the headless data model) since
// this is UI-facing text, not JSON shape.

#include "MidiRemote/RemoteModel.h"

namespace synth {

/** "Tempo (BPM)" / "Playhead Position" / "Master Volume" -- doc-exact display names
 *  (docs/control/midi-remote.md#continuous-targets). */
juce::String continuousTargetDisplayName(ContinuousTargetKind kind);

/** BPM absolute window (docs/control/midi-remote.md#continuous-targets): 60..187, one BPM per 7-bit
 *  CC step (127 steps across 127 BPM). Playhead's window is computed at apply time instead (the loop
 *  region when looping, else the arrangement end) via RemoteActionInvoker::getContinuousWindow --
 *  there is no fixed constant for it. */
inline constexpr double kRemoteBpmWindowMin = 60.0;
inline constexpr double kRemoteBpmWindowMax = 187.0;

/** One relative-encoder detent's native-unit step for bpm/playhead (docs/control/midi-remote.md#continuous-targets):
 *  1 BPM, or 1 beat. */
inline constexpr double kRemoteContinuousRelativeStep = 1.0;

} // namespace synth
