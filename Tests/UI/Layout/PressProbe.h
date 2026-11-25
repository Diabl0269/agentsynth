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

// A press sent through a window's peer asks the OS whether that window is the top one at the point; a test's hidden
// windows sit under whatever is on the developer's screen, so while this lives windows hit-test by their own shape, as
// in the automation launch.
struct IgnoreWindowOrder final {
    IgnoreWindowOrder() { juce::ComponentPeer::ignoreWindowOrderForHitTests = true; }
    ~IgnoreWindowOrder() { juce::ComponentPeer::ignoreWindowOrderForHitTests = previous; }
    const bool previous = juce::ComponentPeer::ignoreWindowOrderForHitTests;
};

} // namespace synth::test
