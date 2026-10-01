#pragma once

#include "UI/Layout/TooltipHelpHandler.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <utility>

// ModuleViewAccessibility.h: what a screen reader says for the keyboard-editable module views (the EQ
// curve and the breakpoint curve editor) and a value-only accessibility handler to speak it. The text
// builders are pure, so the wording is unit-testable without a component or a native peer.

namespace synth::ui {

/** A frequency the way it is spoken: "850 Hz" below 1 kHz, "1.2 kHz" above ("1 kHz" when whole). */
inline juce::String describeFrequency(float hz) {
    if (hz < 1000.0f)
        return juce::String(juce::roundToInt(hz)) + " Hz";
    const auto khz = juce::String(hz / 1000.0f, 1).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return khz + " kHz";
}

/** A gain with an explicit sign: "+3.0 dB", "-2.5 dB", "+0.0 dB". */
inline juce::String describeGainDb(float db) {
    const auto rounded = std::round(db * 10.0f) / 10.0f;
    return (rounded < 0.0f ? "-" : "+") + juce::String(std::abs(rounded), 1) + " dB";
}

/** One EQ band: "Band 2, 1.2 kHz, +3.0 dB, Q 0.7", or "Band 2, off" while the band is switched off.
 *  `bandIndex` is 0-based; the text is 1-based like the handle numbers on the curve. */
inline juce::String describeEqBand(int bandIndex, bool enabled, float freqHz, float gainDb, float q) {
    const auto name = "Band " + juce::String(bandIndex + 1);
    if (!enabled)
        return name + ", off";
    return name + ", " + describeFrequency(freqHz) + ", " + describeGainDb(gainDb) + ", Q " + juce::String(q, 1);
}

/** The curve as a whole while no band is selected: "No band selected, 2 of 4 bands on". */
inline juce::String describeEqCurveSummary(int enabledBands, int totalBands) {
    return "No band selected, " + juce::String(enabledBands) + " of " + juce::String(totalBands) + " bands on";
}

/** One breakpoint: "Point 3 of 5, time 0.25, level 0.80". `index` is 0-based. */
inline juce::String describeCurvePoint(int index, int count, double time, float level) {
    auto timeText =
        juce::String(std::round(time * 1000.0) / 1000.0, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    if (timeText.isEmpty() || timeText == "-0")
        timeText = "0";
    return "Point " + juce::String(index + 1) + " of " + juce::String(count) + ", time " + timeText + ", level " +
           juce::String(level, 2);
}

/** The curve editor as a whole while no point is selected: "Curve with 5 points". */
inline juce::String describeCurveSummary(int count) {
    return "Curve with " + juce::String(count) + (count == 1 ? " point" : " points");
}

/** A read-only accessibility value that asks `getText` each time a screen reader reads it. */
class TextValueInterface : public juce::AccessibilityTextValueInterface {
public:
    explicit TextValueInterface(std::function<juce::String()> getText)
        : getText_(std::move(getText)) {}

    bool isReadOnly() const override { return true; }
    juce::String getCurrentValueAsString() const override { return getText_ ? getText_() : juce::String(); }
    void setValueAsString(const juce::String&) override {}

private:
    std::function<juce::String()> getText_;
};

/** The handler a keyboard-editable view returns from createAccessibilityHandler(): a group whose value
 *  is `getText()`. */
inline std::unique_ptr<juce::AccessibilityHandler> makeValueTextHandler(juce::Component& view,
                                                                        std::function<juce::String()> getText) {
    return std::make_unique<TooltipHelpHandler>(
        view, juce::AccessibilityRole::group, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<TextValueInterface>(std::move(getText))});
}

} // namespace synth::ui
