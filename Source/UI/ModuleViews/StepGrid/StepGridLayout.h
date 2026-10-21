#pragma once

#include "Modules/Sequencer/SequencerLanes.h"
#include <array>
#include <juce_graphics/juce_graphics.h>

namespace synth::ui::stepgrid {

/** Where the parts of the step grid sit for a width and a reveal (0 folded, 1 fully shown) per lane. Pure, so the
 *  card's height and the painted view agree by construction. */
struct StepGridLayout {
    juce::Rectangle<int> tools; ///< The pattern tools row, at the very top.
    juce::Rectangle<int> strip; ///< The page strip's row (its space is always kept).
    juce::Rectangle<int> pads;
    juce::Rectangle<int> chips;
    /** Each lane's box at its FULL height; only the top `visibleHeight[lane]` px of it show while it folds. */
    std::array<juce::Rectangle<int>, kSequencerLaneCount> lanes{};
    std::array<int, kSequencerLaneCount> visibleHeight{};
    int total = 0;

    juce::Rectangle<int> visibleLane(SequencerLane lane) const {
        return lanes[(size_t)lane].withHeight(visibleHeight[(size_t)lane]);
    }
};

/** Tools row, page strip, the Pitch lane (the grid proper), the pads, the chip row, then the other lanes in chip order.
 * A lane whose reveal is 0 takes no room at all. */
StepGridLayout computeLayout(const std::array<float, kSequencerLaneCount>& reveal, int width);

/** All reveals at 0 or 1 from a lane mask. */
std::array<float, kSequencerLaneCount> revealsOf(std::uint16_t lanes);

} // namespace synth::ui::stepgrid
