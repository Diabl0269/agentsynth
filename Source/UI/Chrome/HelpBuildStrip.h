#pragma once

#include "UI/Chrome/HelpBuildButton.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

// The Welcome screen's "Help build" strip (docs/layout/chrome.md#welcome-screen-overlay): the message and the
// HelpBuildButton on a panel with a living synth waveform behind them. The waveform morphs sine, triangle, saw,
// square and back; the button's light, shimmer and heart run off the same clock. One AnimationDriver loop, owned
// here, runs only while the strip is on screen and the Animations setting allows motion, and repaints only the
// strip's own bounds. Under Reduce Motion or Animations: Off nothing moves and the strip rests on a sine.
namespace synth::ui {

class HelpBuildStrip : public juce::Component {
public:
    /** One full cycle of every effect, in ms. */
    static constexpr double kCycleMs = 12000.0;

    HelpBuildStrip();
    ~HelpBuildStrip() override;

    juce::Button& button() { return button_; }
    juce::Label& label() { return label_; }

    /** The waveform at `x01` (0 to 1 across one period, 0 = centre line rising) for `shape`, where 0 is a sine, 1 a
     *  triangle, 2 a saw, 3 a square and 4 a sine again; fractions blend the two neighbours. Result in -1..1. */
    static float waveformSample(float x01, float shape);

    void paint(juce::Graphics& g) override;
    void resized() override;
    void parentHierarchyChanged() override { updateLoop(); }
    void visibilityChanged() override { updateLoop(); }

    bool isMovingForTest() const { return looping_; }
    /** Puts the strip mid-cycle at `phase` as if its loop were running, without a loop (a headless test cannot show
     * it). */
    void setPhaseForTest(float phase);
    float phaseForTest() const { return phase_; }
    /** Runs one cycle's start as the loop does between cycles (a test switches the animation mode first). */
    void startCycleForTest() { startCycle(); }

private:
    void updateLoop();
    void startCycle();

    juce::Label label_;
    HelpBuildButton button_;
    float phase_ = 0.0f;
    bool looping_ = false;
    juce::VBlankAnimatorUpdater vblank_{this};
    AnimationDriver driver_;
};

} // namespace synth::ui
