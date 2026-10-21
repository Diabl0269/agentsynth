#include "StepGridLayout.h"
#include "StepGridModel.h"
#include <cmath>

namespace synth::ui::stepgrid {

namespace {

// A lane places itself `gap * reveal` below the part above and shows `height * reveal` of itself, so a lane fully
// shown adds exactly gap + height, as viewHeightFor counts it.
int placeLane(StepGridLayout& layout, SequencerLane lane, float reveal, int width, int y) {
    const int height = laneHeight(lane);
    const int top = y + (int)std::lround((float)kGap * reveal);
    layout.lanes[(size_t)lane] = {0, top, width, height};
    layout.visibleHeight[(size_t)lane] = (int)std::lround((float)height * reveal);
    return top + layout.visibleHeight[(size_t)lane];
}

} // namespace

StepGridLayout computeLayout(const std::array<float, kSequencerLaneCount>& reveal, int width) {
    StepGridLayout layout;
    layout.tools = {0, 0, width, kToolsRowHeight};
    layout.strip = {0, kToolsRowHeight, width, kPageStripHeight};
    int y = kToolsRowHeight + kPageStripHeight;
    y = placeLane(layout, SequencerLane::Pitch, reveal[(size_t)SequencerLane::Pitch], width, y);
    y += kGap;
    layout.pads = {0, y, width, kPadRowHeight};
    y += kPadRowHeight + kGap;
    layout.chips = {0, y, width, kChipRowHeight};
    y += kChipRowHeight;
    for (int i = (int)SequencerLane::Gate; i < kSequencerLaneCount; ++i)
        y = placeLane(layout, (SequencerLane)i, reveal[(size_t)i], width, y);
    layout.total = y;
    return layout;
}

std::array<float, kSequencerLaneCount> revealsOf(std::uint16_t lanes) {
    std::array<float, kSequencerLaneCount> reveal{};
    for (int i = 0; i < kSequencerLaneCount; ++i)
        reveal[(size_t)i] = (lanes & sequencerLaneBit((SequencerLane)i)) != 0 ? 1.0f : 0.0f;
    return reveal;
}

} // namespace synth::ui::stepgrid
