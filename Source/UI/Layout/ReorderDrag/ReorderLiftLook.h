#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

// ReorderLiftLook.h: how a reorder list draws the row that is lifted under the pointer -- a soft
// shadow, a raised fill and a 1 px accent border, all scaled by the animator's 0..1 lift so the
// look eases in and out with the motion. One look for every list (the mixer's zones rows and send
// rows, the macro port dialog, the knob picker).
namespace synth::ui {

inline void paintReorderLift(juce::Graphics& g, juce::Rectangle<float> bounds, float lift, juce::Colour surface,
                             juce::Colour accent) {
    if (lift <= 0.0f)
        return;
    const auto body = bounds.reduced(1.0f);
    g.setColour(juce::Colours::black.withAlpha(0.22f * lift));
    g.fillRoundedRectangle(body.translated(0.0f, 1.0f), 3.0f);
    g.setColour(surface);
    g.fillRoundedRectangle(body, 3.0f);
    g.setColour(accent.withMultipliedAlpha(lift));
    g.drawRoundedRectangle(body, 3.0f, 1.0f);
}

// The fill and text a lifted *button* keeps. A toggled-on button (the active dock tab) wears the look-and-feel's
// own toggled-on colours while it is lifted, dragged and settling, read from the button so no colour is copied;
// any other button gets the list's raised `surface` and `text`.
struct LiftedButtonLook {
    juce::Colour fill;
    juce::Colour text;
};

inline LiftedButtonLook liftedButtonLook(const juce::TextButton& button, juce::Colour surface, juce::Colour text) {
    if (button.getToggleState())
        return {button.findColour(juce::TextButton::buttonOnColourId),
                button.findColour(juce::TextButton::textColourOnId)};
    return {surface, text};
}

} // namespace synth::ui
