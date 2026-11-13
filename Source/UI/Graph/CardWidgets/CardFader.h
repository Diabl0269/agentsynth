#pragma once

#include "UI/Graph/CardWidgets/CardControlGestures.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth::ui {

/**
 * A module card's fader: a linear juce::Slider painted by AppLookAndFeel's fader painter (the design
 * system's small vertical fader, or its medium horizontal one), with every gesture a card knob has
 * (CardControlGestures: modulation-amount drag, cable pickup, hover, keyboard steps) plus the mixer
 * fader's Shift-fine drag and Cmd-click / double-click reset to the parameter's default (set with
 * setDoubleClickReturnValue). A right click is left to the card's mouse listener (the control menu).
 * docs/layout/module-card.md#faders-switches-and-steppers.
 */
class CardFader
    : public juce::Slider
    , public CardControlGestures {
public:
    enum class Orientation { Vertical, Horizontal };

    explicit CardFader(Orientation orientation);

    /** Width of a vertical fader; narrower than the painter's large-fader threshold on purpose. */
    static constexpr int kVerticalWidth = 40;
    /** The modulation bar's thickness, px. */
    static constexpr float kModBarThickness = 3.0f;
    /** Shift-drag rate. */
    static constexpr double kFineRate = 1.0 / 8.0;

    /** The parameter's own text with its spaces dropped ("1.00 s" reads "1.00s", "50 %" reads "50%"), so a
        value fits the box of a narrow fader without truncating. */
    static juce::String compactValueText(const juce::String& parameterText);
    /** Shows the parameter's text, compacted, in the value box, at the theme's label size and with no side
        padding. Call after the parameter attachment: it installs its own text function, which this replaces
        (typing a value back still parses the full text). */
    void useCompactValueText(const juce::RangedAudioParameter& param);

    bool isVerticalFader() const noexcept { return orientation_ == Orientation::Vertical; }

    // ---- Modulation geometry (fader-local, px) ---------------------------------------------------
    /** The rectangle the cap travels along. */
    juce::Rectangle<float> travelBounds() const;
    /** Where the cap centre sits along the travel for `proportion` (0..1 of the travel). */
    float positionForProportion(double proportion) const;
    /** The strip beside the slot the modulation bar is drawn in, spanning the whole travel. */
    juce::Rectangle<float> modBarTrack() const;
    /** The bar from `fromProportion` to `toProportion` inside modBarTrack(). */
    juce::Rectangle<float> modBarBetween(double fromProportion, double toProportion) const;
    /** True when `local` lands on the bar's strip (the modulation-amount drag's hit zone). */
    bool hitsModBar(juce::Point<float> local) const;
    /** Where a cable landing on this fader ends: just past the bar's zero end. */
    juce::Point<float> landingPoint(float dotDiameter) const;

    /** Cmd-click / double-click: the parameter's default as one change gesture. */
    void resetToDefault();

    bool keyPressed(const juce::KeyPress& key) override {
        return cancelModDotOnEscape(key) || applyValueKey(*this, key);
    }
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseDoubleClick(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;
    void focusGained(FocusChangeType cause) override;
    void focusLost(FocusChangeType cause) override;

private:
    void lookAndFeelChanged() override;
    void styleCompactValueBox();
    void reanchor(juce::Point<float> mouse);

    Orientation orientation_;
    std::optional<juce::Slider::ScopedDragNotification> drag_; ///< Engaged for a whole value drag.
    bool compactValueText_ = false;
    bool shiftWasDown_ = false;
    juce::Point<float> anchorMouse_;
    double anchorProportion_ = 0.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CardFader)
};

/** Paints a fader's modulation bar: `bar` in `colour`, brighter and wider while `hovered`. */
void paintCardFaderModBar(juce::Graphics& g, juce::Rectangle<float> bar, juce::Colour colour, bool hovered);

} // namespace synth::ui
