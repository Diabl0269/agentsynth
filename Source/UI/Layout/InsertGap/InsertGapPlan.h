#pragma once

// InsertGapPlan.h (docs/layout/layout.md#making-room-for-a-module-dropped-between-others): the pure geometry of
// "a module dragged between two cards pushes the cards after it aside". No components, no canvas: one nesting level's
// layout units in, the gap (which unit the new card goes in front of, where it lands, and how far each unit moves)
// out. Unit-tested headlessly (Tests/UI/Graph/InsertGap/InsertGapPlanTests.cpp); the live canvas side is InsertGap.

#include "UI/Layout/LayoutUtil.h"
#include <optional>
#include <vector>

namespace synth::insert_gap {

/** Row: the cards after the insertion point slide right. Column: the cards below it slide down. */
enum class Axis { Row, Column };

/** Where a card goes: in front of `anchorKey` (the first unit after the insertion point) along `axis`. */
struct Target {
    Axis axis = Axis::Row;
    juce::String anchorKey;
    bool operator==(const Target& other) const { return axis == other.axis && anchorKey == other.anchorKey; }
    bool operator!=(const Target& other) const { return !(*this == other); }
};

struct Plan {
    Target target;
    juce::Rectangle<int> slot;               // where the new card lands
    int spacing = 0;                         // the gap kept on each side of it
    std::vector<LayoutUtil::UnitMove> moves; // every unit that moves; deltas only ever point right (row) or down
};

/** The fraction of a card's width (row) or height (column) from either edge that counts as "beside" it. A pointer
 *  deeper inside a card than this is an ordinary drop onto it, which dodges as before. */
inline constexpr float kEdgeBand = 0.25f;
/** The spacing used when the row has no neighbour to copy it from, and the most an insert ever keeps. */
inline constexpr int kDefaultSpacing = 40;
inline constexpr int kMaxSpacing = LayoutUtil::kLayerGapX;

/** Which gap the pointer is over, judged on the units as they are drawn now. `units` are one level's layout units
 *  without the card being placed; `size` is that card's (w, h). Empty when the pointer is over no gap, deep inside
 *  a card, or past the last card of its row or column. Pinned units are never anchors. */
std::optional<Target> pickTarget(const std::vector<LayoutUtil::LayoutUnit>& units, juce::Point<int> pointer,
                                 juce::Point<int> size);

/** The gap that puts a `size` card in front of `target.anchorKey`, on `units` at home (before any gap opened).
 *  Empty when the anchor is missing or pinned; a plan with no moves means the card already fits. */
std::optional<Plan> planBefore(const std::vector<LayoutUtil::LayoutUnit>& units, const Target& target,
                               juce::Point<int> size);

/** The gap for a card placed right after `afterKey` in its row (the keyboard's "insert after the selected card"):
 *  planBefore the next unit of that row. Empty when nothing follows it. */
std::optional<Plan> planAfter(const std::vector<LayoutUtil::LayoutUnit>& units, const juce::String& afterKey,
                              juce::Point<int> size);

/** The next unit after `unitKey` along `axis` (it overlaps the unit across the axis and its centre lies beyond the
 *  unit's), nearest first; empty when there is none. */
std::optional<juce::String> nextAfter(const std::vector<LayoutUtil::LayoutUnit>& units, const juce::String& unitKey,
                                      Axis axis);

/** A unit that grew from `before` to `after` (an open macro whose members were pushed apart) pushes the units ahead of
 *  it along `axis` that it now covers, in chain, the way planBefore's movers do. `units` leaves the grown one out. */
std::vector<LayoutUtil::UnitMove> pushAhead(const std::vector<LayoutUtil::LayoutUnit>& units,
                                            juce::Rectangle<int> before, juce::Rectangle<int> after, Axis axis);

/** `v` rounded up to the grid (kGridSize); values at or below 0 give 0. */
int snapUp(int v) noexcept;

} // namespace synth::insert_gap
