// PressProbe.h -- a component that counts the presses and releases that reach it: put it under a popup to see whether
// a press the popup closed on landed where it was aimed.
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::test {

struct PressProbe final : juce::Component {
    void mouseDown(const juce::MouseEvent& e) override {
        ++downs;
        lastDown = e.getScreenPosition();
    }
    void mouseUp(const juce::MouseEvent&) override { ++ups; }
    int downs = 0;
    int ups = 0;
    juce::Point<int> lastDown;
};

} // namespace synth::test
