#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

// Pure geometry for the Cmd-hold shortcut hints: no Components, no LookAndFeel, so every placement
// rule is unit-testable with plain rectangles (docs/control/shortcuts.md#shortcut-hints).
namespace synth::ui::hint {

/** How far a bubble overlaps the bottom edge of the button it labels. */
inline constexpr int kBubbleOverlap = 4;
/** Gap between a dock tab's name and the bubble inside that tab. */
inline constexpr int kTabNameGap = 6;
/** Bottom-panel-hidden row: pill height, gap between pills, and gap above the status bar. */
inline constexpr int kPillHeight = 20;
inline constexpr int kPillGap = 6;
inline constexpr int kRowBottomMargin = 8;

struct BubbleRequest {
    juce::Rectangle<int> anchor;    // the labelled button, in overlay coordinates
    juce::Point<int> size;          // the bubble's width/height
    juce::Rectangle<int> container; // the bubble may not leave this (empty = the window only)
};

/** Centred under `anchor`, overlapping its bottom edge; flipped above when that would leave the
 *  window or `container`; nudged sideways to stay inside the window. Empty when neither fits. */
std::optional<juce::Rectangle<int>> placeBubble(const BubbleRequest& request, juce::Rectangle<int> window);

/** Places every request in order. A later bubble that overlaps an earlier one slides sideways by
 *  the overlap (at most half its width); if it still overlaps, or no place fits, its slot is empty. */
std::vector<std::optional<juce::Rectangle<int>>> placeBubbles(const std::vector<BubbleRequest>& requests,
                                                              juce::Rectangle<int> window);

/** The bubble inside a dock tab: `kTabNameGap` after the name (which ends at `nameRight`), centred
 *  vertically. Empty when it would not fit inside the tab. */
std::optional<juce::Rectangle<int>> placeInsideTab(juce::Rectangle<int> tab, int nameRight, juce::Point<int> size);

/** The bottom-panel-hidden row: one pill per width, in order, centred along the window bottom,
 *  `kRowBottomMargin` above `statusBarTop`. */
std::vector<juce::Rectangle<int>> layoutHiddenRow(const std::vector<int>& pillWidths, int pillHeight,
                                                  juce::Rectangle<int> window, int statusBarTop);

} // namespace synth::ui::hint
