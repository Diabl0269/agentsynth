#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The timeline's Draw (pen) tool button. It paints the CURRENT draw shape's icon (crossfading old to new when the
// shape changes) and a small triangle in its bottom-right corner. A click on the corner, or a press held for
// kHoldMs, asks the owner to open the shape flyout; a click anywhere else picks the tool like any tool button.
// Message thread only.
class DrawPenButton
    : public juce::DrawableButton
    , private juce::Timer {
public:
    static constexpr int kCornerSize = 10;
    static constexpr int kHoldMs = 400;
    static constexpr double kCrossfadeMs = 160.0;
    static constexpr double kCrossfadeReducedMs = 80.0;

    explicit DrawPenButton(const juce::String& name);
    ~DrawPenButton() override;

    /** The shape whose icon the button shows; a change crossfades (at once under Animations: Off or off screen). */
    void setShape(DrawShape shape);
    DrawShape getShape() const noexcept { return shape_; }
    /** 0..1: how far the crossfade from the previous icon has got (1 at rest). */
    float getCrossfade() const noexcept { return crossfade_; }

    /** True for a point (button coordinates) inside the corner triangle's hit area. */
    bool isInCorner(juce::Point<int> p) const noexcept;

    /** The owner opens the flyout here. */
    std::function<void()> onOpenFlyout;

    /** Test seam: what the hold timer does when it fires (headless tests have no message loop). */
    void holdElapsedForTest() { timerCallback(); }
    bool isHoldPendingForTest() const noexcept { return isTimerRunning(); }

protected:
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    DrawShape shape_ = DrawShape::Free;
    DrawShape previous_ = DrawShape::Free;
    float crossfade_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
    bool cornerPress_ = false;
    bool holdFired_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawPenButton)
};

} // namespace synth::ui
