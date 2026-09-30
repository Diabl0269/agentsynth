#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"

// Pure clip-to-clip navigation for the timeline's keyboard mode. No components and no view state:
// every function reads the doc and answers with a ClipId, invalid when there is nowhere to go.
namespace synth::ui::clipnav {

/** The first clip starting at or after `beat`, else the track's first clip, else invalid. */
synth::ClipId firstClipFrom(const synth::Track& track, double beat);

/** The clip before (direction < 0) or after (direction > 0) `id` on its own track, in start order.
 *  Invalid at either end or when `id` is not on the track. */
synth::ClipId adjacentOnTrack(const synth::Track& track, synth::ClipId id, int direction);

/** The clip nearest in start time to `id` on the closest track above (direction < 0) or below
 *  (direction > 0) that has any clips; ties go to the earlier clip. Tracks without clips are
 *  skipped. Invalid when no such track exists. */
synth::ClipId nearestOnAdjacentTrack(const synth::TimelineDoc& doc, synth::ClipId id, int direction);

} // namespace synth::ui::clipnav
