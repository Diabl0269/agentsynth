#pragma once

#include "UI/Chrome/ToolbarButton/SideMenuMark.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The timeline's Draw (pen) tool button. It paints the CURRENT draw shape's icon, centred in the full button
// (crossfading old to new when the shape changes), with the shared menu notch in its lower-right corner. A right-click,
// a press in the corner or a press held for SideMenuGesture::kHoldMs asks the owner to open the shape flyout; a plain
// click picks the tool like any tool button. Message thread only.
class DrawPenButton : public juce::DrawableButton {
public:
    static constexpr double kCrossfadeMs = 160.0;
    static constexpr double kCrossfadeReducedMs = 80.0;

    explicit DrawPenButton(const juce::String& name);
    ~DrawPenButton() override;

    /** The shape whose icon the button shows; a change crossfades (at once under Animations: Off or off screen). */
    void setShape(DrawShape shape);
    DrawShape getShape() const noexcept { return shape_; }
    /** 0..1: how far the crossfade from the previous icon has got (1 at rest). */
    float getCrossfade() const noexcept { return crossfade_; }

    /** The owner opens the flyout here. */
    std::function<void()> onOpenFlyout;
    /** After a press-hold opened the flyout, the release lands here (screen position) before the hold ends, so the
     *  owner can pick the row it ended on. */
    std::function<void(juce::Point<int>)> onHoldReleased;
    /** The gesture that opens the flyout; tests stand in for its hold timer. */
    SideMenuGesture& getMenuGesture() noexcept { return gesture_; }
    bool isHoldGestureDown() const noexcept { return gesture_.isHoldGestureDown(); }

protected:
    void paintButton(juce::Graphics& g, bool over, bool down) override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

private:
    DrawShape shape_ = DrawShape::Free;
    DrawShape previous_ = DrawShape::Free;
    float crossfade_ = 1.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver fade_;
    SideMenuGesture gesture_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawPenButton)
};

} // namespace synth::ui
