#pragma once

// The on-card layout editor's geometry as pure functions over rectangles in card pixels, so snapping and
// pushing are unit-testable with no component: where a dragged control lands against its neighbours'
// lines, and where the neighbours go when it is dropped on them. docs/layout/module-card-layout.md#editing-a-layout.

#include <juce_core/juce_core.h>
#include <juce_graphics/juce_graphics.h>
#include <vector>

namespace synth::ui::oncard {

/** The clear space controls keep from each other, and how close a line must come to pull a drag in. */
inline constexpr int kControlGap = 8;
inline constexpr int kSnapDistance = 4;
/** The arrow-key step, and the Shift step. */
inline constexpr int kNudgeStep = 1;
inline constexpr int kNudgeBigStep = 8;

/** An alignment line: `position` is its x (vertical) or y, spanning `from`..`to` on the other axis. */
struct Guide {
    bool vertical = true;
    int position = 0;
    int from = 0;
    int to = 0;

    bool operator==(const Guide& other) const noexcept {
        return vertical == other.vertical && position == other.position && from == other.from && to == other.to;
    }
};

struct SnapResult {
    juce::Rectangle<int> rect;
    std::vector<Guide> guides;
};

/** Where a section's controls may sit: x within [minX, maxX], y from `top` down (a drop below the
 *  section grows it). */
struct Limits {
    int minX = 0;
    int maxX = 0;
    int top = 0;
};

/** `dragged` pulled up to 4 px onto any left/centre/right (x) and top/centre/bottom (y) line of
 *  `others`, with a guide for each axis that snapped. `enabled` false (Cmd held) places it freely. */
SnapResult snapDraggedRect(juce::Rectangle<int> dragged, const std::vector<juce::Rectangle<int>>& others, bool enabled);

/** `rect` moved inside `limits` (kept whole inside x, never above the top). */
juce::Rectangle<int> clampToLimits(juce::Rectangle<int> rect, const Limits& limits);

/** The rects of `others`, same order, after the control that stood at `start` is dropped at `dropped`:
 *  a cell closer than the gap to the dropped one (or to one already pushed) goes the shortest way out
 *  among left, right, up and down that stays inside `limits` and clear of every fixed cell, else below
 *  everything. A cell that already abutted `start` stays put unless the drop truly overlaps it, so
 *  moving a control one pixel does not shove the flush rows around it. */
std::vector<juce::Rectangle<int>> pushAside(juce::Rectangle<int> dropped, juce::Rectangle<int> start,
                                            const std::vector<juce::Rectangle<int>>& others, const Limits& limits);

/** What a screen reader hears after a move: "Cutoff moved right 8", or "Cutoff moved right 60, down 12". */
juce::String describeMove(const juce::String& caption, int dx, int dy);

/** True when `a` and `b` are closer than the gap (overlapping included). */
bool tooClose(juce::Rectangle<int> a, juce::Rectangle<int> b);

} // namespace synth::ui::oncard
