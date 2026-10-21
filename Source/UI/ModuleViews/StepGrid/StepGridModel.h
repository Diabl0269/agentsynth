#pragma once

#include "Modules/Sequencer/SequencerLanes.h"
#include "Modules/Sequencer/SequencerPattern.h"
#include <juce_core/juce_core.h>
#include <utility>
#include <vector>

// The step grid's pure side: sizes, the pitch range of the mini piano roll, how a lane value maps to a bar and back,
// and the words a screen reader hears. Nothing here draws or touches a module, so it is unit-tested directly.
// docs/modules/sequencer-design.md#card.
namespace synth::ui::stepgrid {

constexpr int kColumns = 16;     ///< Steps per page.
constexpr int kGutterWidth = 44; ///< Lane names / note names, left of the columns.
constexpr int kRightMargin = 2;
constexpr int kGap = 4;             ///< Between the stacked parts.
constexpr int kToolsRowHeight = 28; ///< The pattern tools row above the page strip.
constexpr int kPageStripHeight = 20;
constexpr int kPitchLaneHeight = 112;
constexpr int kBarLaneHeight = 40;
constexpr int kMarkLaneHeight = 24; ///< Slide and Condition.
constexpr int kPadRowHeight = 18;
constexpr int kChipRowHeight = 22;
constexpr int kMinPitchRows = 13; ///< An octave and a note.
constexpr int kMaxPitchRows = 36; ///< Three octaves.

int laneHeight(SequencerLane lane) noexcept;
/** The name on the chip and the lane ("Pitch", "Gate"). */
juce::String laneName(SequencerLane lane);
/** The lanes the chips offer, in chip order. */
std::vector<SequencerLane> availableLanes(bool poly);
/** The lane mask with lanes the module cannot show removed. */
std::uint16_t sanitizeLanes(std::uint16_t mask, bool poly) noexcept;

/** The step grid's whole height with `lanes` shown: tools row, page strip, the Pitch lane, pads, chips, the other
 * lanes. */
int viewHeightFor(std::uint16_t lanes, bool poly);
/** The part of that height that does not depend on the lanes. */
int fixedHeight(bool poly);

int pageCount(int steps) noexcept;
/** "1-16", "17-32", the last one ending at `steps`. */
juce::String pageLabel(int page, int steps);
/** "Page 2, steps 17 to 32". */
juce::String pageDescription(int page, int steps);

/** Octave numbering where middle C (MIDI 60) is C3, like the rest of the app. */
juce::String noteName(int pitch);

struct PitchRange {
    int lo = 48;
    int hi = 60;
    int rows() const noexcept { return hi - lo + 1; }
};
/** The rows of the piano roll: the pitches' span plus a note either side, at least kMinPitchRows and at most
 *  kMaxPitchRows, inside 0..127. */
PitchRange fitPitchRange(const std::vector<int>& pitches);

/** A bar lane's value as 0..1 (bipolar Nudge: 0.5 is no nudge). Slide, Condition and Pitch are not bar lanes. */
bool isBarLane(SequencerLane lane) noexcept;
float laneNormal(SequencerLane lane, const SequencerStep& step) noexcept;
void setLaneNormal(SequencerLane lane, SequencerStep& step, float normal) noexcept;
/** The value as spoken and as shown: "chance 100 percent", "nudge 25 percent early". */
juce::String laneValueText(SequencerLane lane, const SequencerStep& step);

/** The condition menu, in menu order: 1/1, 1/2, 2/2, 1/3..3/3, 1/4..4/4, 1/8..8/8 (hit, of). */
const std::vector<std::pair<int, int>>& conditionChoices();
juce::String conditionText(const SequencerStep& step);

} // namespace synth::ui::stepgrid
