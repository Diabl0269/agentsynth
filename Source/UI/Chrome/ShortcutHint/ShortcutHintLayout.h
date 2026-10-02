#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>
#include <vector>

// Pure geometry for the Cmd-hold shortcut hints: no Components, no LookAndFeel, so every placement
// rule is unit-testable with plain rectangles (docs/control/shortcuts.md#shortcut-hints).
namespace synth::ui::hint {

/** How far a bubble overlaps the bottom edge of the button it labels. */
inline constexpr int kBubbleOverlap = 4;
/** The gap between a bubble and one staggered into the next row when neighbours would collide. */
inline constexpr int kStaggerGap = 2;
/** How many extra rows a colliding bubble may be staggered into before it is left out. */
inline constexpr int kMaxStaggerRows = 2;
/** Gap between a dock tab's name and the bubble inside that tab. */
inline constexpr int kTabNameGap = 6;
/** Bottom-panel-hidden row: pill height, gap between pills, and gap above the status bar. */
inline constexpr int kPillHeight = 24;
inline constexpr int kPillGap = 6;
inline constexpr int kRowBottomMargin = 8;
/** Entrance motion: a bubble starts at this fraction of its size, and a hidden-panel pill rises
 *  from this many pixels below its slot. */
inline constexpr float kBubbleStartScale = 0.6f;
inline constexpr float kPillRisePx = 12.0f;

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

/** Where a bubble sits relative to what it labels, which decides where it grows out of. */
enum class BubbleKind {
    BelowAnchor, // under a button (slides down out of the button)
    AboveAnchor, // flipped above a button (slides up out of the button)
    InsideTab,   // right of a tab's name (slides right out of the tab)
    HiddenRow    // a pill in the bottom-panel-hidden row (no anchor; rises)
};

/** Classifies a bubble placed by placeBubble(): above when it sits over the anchor's top half. */
BubbleKind kindOfPlacedBubble(juce::Rectangle<int> bubble, juce::Rectangle<int> anchor);

/** The point a bubble grows out of at the start of its entrance: the anchor's centre for the three
 *  anchored kinds (`anchor` is the button, or the tab for InsideTab), and `target`'s centre shifted
 *  `kPillRisePx` down for HiddenRow (which ignores `anchor`). */
juce::Point<float> bubbleOrigin(BubbleKind kind, juce::Rectangle<float> target, juce::Rectangle<float> anchor);

/** The bubble's rectangle `t` of the way through its entrance: at t=0 it is `kBubbleStartScale` of
 *  `target`'s size and centred on `origin`; at t=1 it is `target`. Centre and size interpolate
 *  linearly; `t` is clamped to [0, 1]. The bubble's opacity is `t` too. */
juce::Rectangle<float> animatedBubbleBounds(juce::Rectangle<float> target, juce::Point<float> origin, float t);

/** The tween value while fading in from `from` (the value when the fade began); `easedProgress` is
 *  the eased 0..1 progress of the fade. Lets a fade that interrupts a fade-out pick up where it is. */
inline float tweenUp(float from, float easedProgress) { return from + (1.0f - from) * easedProgress; }
/** The tween value while fading out from `from`. */
inline float tweenDown(float from, float easedProgress) { return from * (1.0f - easedProgress); }
/** How long a fade-in that starts at `from` runs, out of the full `fullMs` (never zero). */
inline double resumeDurationMs(float from, double fullMs) { return juce::jmax(1.0, (1.0 - (double)from) * fullMs); }

/** The bottom-panel-hidden row: one pill per width, in order, centred along the window bottom,
 *  `kRowBottomMargin` above `statusBarTop`. */
std::vector<juce::Rectangle<int>> layoutHiddenRow(const std::vector<int>& pillWidths, int pillHeight,
                                                  juce::Rectangle<int> window, int statusBarTop);

} // namespace synth::ui::hint
