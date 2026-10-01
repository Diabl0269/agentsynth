#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>
#include <vector>

// Body-layout metrics and the run layouts a card body is built from: a stack of combos, rows of
// toggles, a knob grid and a full-width view. Each run takes its widgets (a null entry takes its space
// unpainted: a plan never built, or an empty swap cell) and a y cursor and returns the y below it; it positions widgets
// only when `apply` is true, so one function both measures and places (docs/layout/module-card.md#body-layout).
namespace synth::cardbody {

// Three knobs per row: the body sits below every jack, so it can use nearly the full card width.
inline constexpr int kKnobColumns = 3;
inline constexpr int kContentMargin = 12;       // left/right gutter for body content
inline constexpr int kNarrowContentWidth = 200; // combos/toggles/load row stay this narrow, centred
inline constexpr int kLabelHeight = 18;
inline constexpr int kRowHeight = 24;       // combo box / toggle / button
inline constexpr int kKnobHeight = 58;      // rotary + its text box
inline constexpr int kKnobLargeHeight = 80; // a 60 px dial + its text box
inline constexpr int kFaderVHeight = 96;    // a vertical fader's travel + its text box
inline constexpr int kFaderHHeight = 28;    // a horizontal fader, cap, modulation bar and text box
inline constexpr int kWaveformHeight = 72;
inline constexpr int kSectionHeaderHeight = 18; // a titled section's header row
inline constexpr int kBottomPadding = 12;
// A port label box spans its jack centre +/- 10; clear it by a bit more before placing any content.
inline constexpr int kPortLabelClearance = 15;
inline constexpr int kFooterRowHeight = 28;  // one footer row: a horizontal fader's height
inline constexpr int kFooterGap = 6;         // between footer items
inline constexpr int kFooterRowGap = 4;      // between wrapped footer rows
inline constexpr int kFooterMinStretch = 72; // the narrowest a footer fader or combo gets

// The card chrome toggles' text; the size estimate measures the footer pills from the same strings.
inline constexpr const char* kShowScopeText = "Show Scope";
inline constexpr const char* kShowEnvelopeText = "Show Envelope Graph";
inline constexpr const char* kShowResponseText = "Show Response";
inline constexpr const char* kShowSpectrumText = "Show Spectrum";

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
/** layoutKnobRun with any cell height; a `widgetWidth` above 0 centres a narrower widget in its cell. */
int layoutGridRun(const std::vector<CaptionedWidget>& cells, int columns, int cellHeight, int widgetWidth, int y,
                  const BodyGeometry& g, bool apply);
/** One captioned widget per row, `rowHeight` tall, across the content width or (`wide` false) the
 *  narrow band. */
int layoutCaptionedRows(const std::vector<CaptionedWidget>& rows, int rowHeight, bool wide, int y,
                        const BodyGeometry& g, bool apply);
/** One full-width view `height` tall. */
int layoutViewRow(juce::Component* view, int height, int y, const BodyGeometry& g, bool apply);

} // namespace synth::cardbody
