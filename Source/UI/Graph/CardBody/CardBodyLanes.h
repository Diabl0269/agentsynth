#pragma once

// The lane grid's metrics and its two pure steps, so they can be tested without a card: packing a lane's cells into
// columns, and sharing the spare width among lanes. The lane grid itself is laid out in CardBodyLanes.cpp.
// docs/layout/module-card-layout.md#lane-presentation.

#include "UI/Graph/CardBody/CardBodyGeometry.h"
#include <vector>

namespace synth::cardbody {

/** No control, pill or value box comes closer than this to a lane's hairline. */
inline constexpr int kLaneClear = 6;
inline constexpr int kLaneRuleWidth = 1;
inline constexpr int kLaneColumnGap = 4; ///< Between two columns of one lane.
/** A lane title's text row, then its 1 px hairline, then kLaneTitleGap before the first control. */
inline constexpr int kLaneTitleHeight = 16;
inline constexpr int kLaneTitleGap = 5;
inline constexpr int kLaneTitleBand = kLaneTitleHeight + kLaneRuleWidth + kLaneTitleGap;
inline constexpr float kLaneTitleFontHeight = 9.0f;
inline constexpr float kLaneTitleKerning = 0.09f;

// The Out lane's small knob: a caption box, a gap, a ~38 px dial and a value box under it, three of them stacked inside
// the band.
inline constexpr int kSmallKnobCellWidth = 52;
inline constexpr int kSmallKnobCaptionHeight = kCaptionBoxHeight; ///< as tall as its text, descender included
/** Between a small knob's caption box and its dial: half the normal caption gap, so three knobs still fit. */
inline constexpr int kSmallKnobCaptionGap = kCaptionGap / 2;
inline constexpr int kSmallKnobWidgetHeight = 50;
inline constexpr int kSmallKnobTextBoxHeight = 12;
inline constexpr int kSmallKnobTextBoxWidth = 48;
inline constexpr int kSmallKnobCellHeight = kSmallKnobCaptionHeight + kSmallKnobCaptionGap + kSmallKnobWidgetHeight;

/** Between two cells stacked in a lane's column, so their edit outlines and captions never touch. */
inline constexpr int kLaneCellGap = 8;
/** A sequencer's Key field reads "Song key . E Natural Minor", which a 96 px choice cell would cut short. */
inline constexpr int kKeyFieldWidth = 184;
/** What a lane of ordinary cells stacks against: two knob cells fit it (18 caption + 58 knob, twice, and a gap). */
inline constexpr int kLaneStackBand = 2 * (kLabelHeight + kKnobHeight) + kLaneCellGap;
/** A lane grid's band: the Out lane's three small knobs (two knob cells and a gap fit it too); a taller control grows
 *  it. */
inline constexpr int kLaneBandHeight = 3 * kSmallKnobCellHeight + 2 * kLaneCellGap;

/** The mode row's smallest selector per segment, the gap between its zones, and the control a right-end choice draws
 *  after its inline caption. */
inline constexpr int kModeSegmentWidth = 50;
inline constexpr int kModeInlineControlWidth = 72;

/** Packs cells of the given heights, in order, into columns of a band `band` high: a column is filled top to bottom
 *  while the next cell still fits, then the next column starts. Returns each column's cell indices. */
std::vector<std::vector<int>> packLaneColumns(const std::vector<int>& heights, int band);

/** The width each lane takes: its `needs` (content plus the clear space beside its hairlines) and an even share of
 *  what is left of `available` (a 1 px hairline between each two lanes comes off first); a remainder goes to the
 *  first lanes. When the lanes need more than `available` they get only their needs. */
std::vector<int> shareLaneWidths(const std::vector<int>& needs, int available);

/** The card width a lane grid of `lanesWidth` (lanes, clear space and hairlines) and a mode row of `modeWidth` need:
 *  the wider of the two plus the side margins, up to a multiple of 4. */
int laneCardWidthFor(int lanesWidth, int modeWidth);

} // namespace synth::cardbody
