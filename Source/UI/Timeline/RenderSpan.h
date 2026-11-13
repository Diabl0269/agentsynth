#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"

namespace synth::ui {

/** One track's stretch of time to render to an audio track: [startBeat, endBeat) on `track`. */
struct RenderSpan {
    synth::TrackId track;
    double startBeat = 0.0;
    double endBeat = 0.0;
};

} // namespace synth::ui
