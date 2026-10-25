#pragma once

#include "DrumGrid.h"
#include <juce_core/juce_core.h>

namespace synth::ui {

/**
 * What a grid cell shows about its hit beyond the loudness fill, as plain state a test can read and a painter can
 * draw (`DrumMachineComponent::paintCellMarks`). Chance below 100 dims the fill and adds a "%" mark; repeat above 1
 * draws that many ticks across the top; a flam draws a grace tick at the left; a condition other than always writes
 * "2/4" (a dot when the cell is too narrow); a hit that plays off its cell start (an odd step under swing, or a nudge)
 * gets a tick where it plays. docs/timeline/drum-machine.md#grid-marks.
 */
struct DrumCellMarks {
    bool dimmed = false;
    int repeatTicks = 0; ///< 0 when the hit plays once
    bool flam = false;
    juce::String condition; ///< "2/4", empty for always
    bool playsOffStart = false;
    double playOffset = 0.0; ///< where the hit plays, in steps from the cell's start (swing plus nudge); may be < 0

    bool any() const noexcept { return dimmed || repeatTicks > 1 || flam || condition.isNotEmpty() || playsOffStart; }
};

inline DrumCellMarks describeCellMarks(const drumgrid::CellHits& hits, double swing, int step) {
    DrumCellMarks marks;
    if (hits.empty())
        return marks;
    marks.dimmed = hits.chance < 100;
    marks.repeatTicks = hits.repeat > 1 ? hits.repeat : 0;
    marks.flam = hits.flam;
    if (hits.condOf > 1)
        marks.condition = juce::String(hits.condHit) + "/" + juce::String(hits.condOf);
    const double swung = swing > 0.5 && (step & 1) != 0 ? (swing - 0.5) * 2.0 : 0.0;
    marks.playOffset = swung + hits.nudge;
    marks.playsOffStart = std::abs(marks.playOffset) > 1.0e-9;
    return marks;
}

} // namespace synth::ui
