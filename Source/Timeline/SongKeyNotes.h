#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <array>
#include <functional>

namespace synth {

/** How long each pitch class C..B sounds across the song's MIDI tracks, in beats, for detectKey(). Muted tracks,
 *  clips and notes are left out, and so is every track `skipTrack` returns true for (drum kits). */
std::array<double, 12> songPitchClassWeights(const TimelineDoc& doc,
                                             const std::function<bool(const Track&)>& skipTrack = {});

} // namespace synth
