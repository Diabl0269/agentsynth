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

} // namespace synth::ui
