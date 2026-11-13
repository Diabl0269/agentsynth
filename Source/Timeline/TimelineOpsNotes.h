#pragma once

// Reads one note of a timelineOps `placeClips` clip. Split out of TimelineOps.cpp by concern (that file is at its
// size cap). The error carries no "timelineOps[i] (op): " prefix; the caller adds it.

#include "Timeline/TimelineDoc/TimelineDoc.h"

#include <juce_core/juce_core.h>

namespace synth {

/** Reads `noteVar` into `out`. Every key is optional: startBeat, lengthBeats, pitch, velocity, channel and the
 *  per-hit drum settings chance (0..100), repeat (1..8), flam (bool), nudge (-0.5..0.5) and condition ("h/o",
 *  1 <= h <= o <= 8). Out-of-range values are rejected, never clamped. Empty on success, else the failure text. */
juce::String readNoteOp(const juce::var& noteVar, MidiNote& out);

} // namespace synth
