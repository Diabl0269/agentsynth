#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>
#include <vector>

// Body-layout metrics and the run layouts a card body is built from: a stack of combos, rows of
// toggles, a knob grid and a full-width view. Each run takes its widgets (null entries measure only)
// and a y cursor and returns the y below it; it positions widgets only when `apply` is true, so one
// function both measures and places (docs/layout/module-card.md#body-layout).
namespace synth::cardbody {

// Three knobs per row: the body sits below every jack, so it can use nearly the full card width.
inline constexpr int kKnobColumns = 3;
inline constexpr int kContentMargin = 12;       // left/right gutter for body content
inline constexpr int kNarrowContentWidth = 200; // combos/toggles/load row stay this narrow, centred
inline constexpr int kLabelHeight = 18;
inline constexpr int kRowHeight = 24;  // combo box / toggle / button
inline constexpr int kKnobHeight = 58; // rotary + its text box
inline constexpr int kWaveformHeight = 72;
inline constexpr int kBottomPadding = 12;
// A port label box spans its jack centre +/- 10; clear it by a bit more before placing any content.
inline constexpr int kPortLabelClearance = 15;

/** Where body content may go on a card of `width`. */
struct BodyGeometry {
    int width = 0;
    int contentX = 0;
    int contentW = 0;
    int narrowX = 0; ///< The centred band combos use.
    int narrowW = 0;

    static BodyGeometry forCardWidth(int width);
    bool isDoubleWidth() const;
};

/** A widget and the caption above it; either may be null (measure only). */
using CaptionedWidget = std::pair<juce::Component*, juce::Component*>;

/** Combos with their labels: one per row, or two per row on a double-width card. */
int layoutChoiceRun(const std::vector<CaptionedWidget>& combos, int y, const BodyGeometry& g, bool apply);
/** Toggles, one full-width row each. */
int layoutToggleRun(const std::vector<juce::Component*>& toggles, int y, const BodyGeometry& g, bool apply);
/** Knobs with their labels, `columns` per row (doubled on a double-width card). */
int layoutKnobRun(const std::vector<CaptionedWidget>& knobs, int columns, int y, const BodyGeometry& g, bool apply);
/** One full-width view `height` tall. */
int layoutViewRow(juce::Component* view, int height, int y, const BodyGeometry& g, bool apply);

} // namespace synth::cardbody
