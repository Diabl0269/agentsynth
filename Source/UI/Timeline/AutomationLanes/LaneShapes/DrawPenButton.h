#pragma once

#include "UI/Layout/FadeAmount.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The timeline's Draw (pen) tool button. It paints the CURRENT draw shape's icon (crossfading old to new when the
// shape changes) and, as a split button (design-system SplitButton), a chevron half at its right edge behind a thin
// divider. A click on the chevron half, a right-click, or a press held for kHoldMs asks the owner to open the shape
// flyout; a click on the icon half picks the tool like any tool button. The chevron half lights under the pointer and
// has its own tooltip ("More shapes"). Message thread only.
class DrawPenButton
    : public juce::DrawableButton
    , private juce::Timer {
public:
    /** Width of the chevron half, in button pixels. The owner makes the button this much wider than a plain tool. */
    static constexpr int kChevronWidth = 14;
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

    /** True for a point (button coordinates) inside the chevron half, the part that opens the flyout. */
    bool isInChevron(juce::Point<int> p) const noexcept;
    /** The icon half, in button coordinates. */
    juce::Rectangle<int> getIconHalf() const noexcept { return getLocalBounds().withTrimmedRight(kChevronWidth); }
    /** How far the chevron half's hover wash has risen, 0..1. */
    float getChevronHoverForTest() const noexcept { return chevronHover_.value(); }

    /** The tip over the chevron half (names what it opens and its key); the rest of the button keeps setTooltip's. */
    void setChevronTooltip(const juce::String& tip) { chevronTip_ = tip; }
    juce::String getTooltip() override { return getTooltipAt(getMouseXYRelative()); }
    /** The tip for a pointer at `p` (button coordinates). */
    juce::String getTooltipAt(juce::Point<int> p);

    /** The owner opens the flyout here. */
    std::function<void()> onOpenFlyout;
    /** After a press-hold opened the flyout, the release lands here (screen position) before the hold ends, so the
     *  owner can pick the row it ended on. */
    std::function<void(juce::Point<int>)> onHoldReleased;
    /** True from the moment a hold opened the flyout until that press is released. */
    bool isHoldGestureDown() const noexcept { return holdFired_; }

    /** Test seam: what the hold timer does when it fires (headless tests have no message loop). */
    void holdElapsedForTest() { timerCallback(); }
    bool isHoldPendingForTest() const noexcept { return isTimerRunning(); }

protected:
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    void timerCallback() override;

    DrawShape shape_ = DrawShape::Free;
    DrawShape previous_ = DrawShape::Free;
    float crossfade_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
    FadeAmount chevronHover_{*this};
    juce::String chevronTip_;
    bool chevronPress_ = false;
    bool holdFired_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawPenButton)
};

} // namespace synth::ui
