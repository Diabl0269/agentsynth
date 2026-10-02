#pragma once

#include "UI/Layout/UIAnimation.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace synth::ui {

// The Draw tool's shape buttons: one small icon button per DrawShape that slides out of the right side
// of the Draw button while Draw is the active tool and slides back when another tool is picked. The
// owner lays the strip out at getCurrentWidth() and re-lays it out from onSlideFrame. Message thread only.
class DrawShapeStrip : public juce::Component {
public:
    static constexpr int kButtonWidth = 26;
    static constexpr double kSlideInMs = 160.0;
    static constexpr double kSlideOutMs = 110.0;

    /** `animationHost` (the owning panel) drives the slide's frames; it must outlive the strip. */
    explicit DrawShapeStrip(juce::Component& animationHost);
    ~DrawShapeStrip() override;

    /** Slides out (true) or back (false); lands at once while the host is not on screen. */
    void setShowing(bool showing);
    bool isShowingTarget() const noexcept { return slide_.getTarget() > 0.5f; }
    const PanelSlide& getSlide() const noexcept { return slide_; }
    int getOpenWidth() const noexcept { return kButtonWidth * (int)kAllDrawShapes.size(); }
    /** The width layout gives the strip right now, proportional to the slide. */
    int getCurrentWidth() const noexcept { return slide_.sizeBetween(0, getOpenWidth()); }

    /** Lights the button of `shape` (no callback). */
    void setActiveShape(DrawShape shape);
    /** Never null. */
    juce::DrawableButton* getButton(DrawShape shape) const noexcept;
    /** Re-tints the icons and the active highlight; null (headless) keeps the defaults. */
    void applyTheme(juce::LookAndFeel* lookAndFeel);

    /** A shape button was clicked. */
    std::function<void(DrawShape)> onShapeClicked;
    /** Called on every slide frame (and once when it lands) so the owner re-lays the strip out. */
    std::function<void()> onSlideFrame;

    void resized() override;

private:
    void slideFrame();

    juce::Component& animationHost_;
    juce::VBlankAnimatorUpdater vblank_;
    PanelSlide slide_;
    AnimationDriver slideAnim_;
    std::array<std::unique_ptr<juce::DrawableButton>, kAllDrawShapes.size()> buttons_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DrawShapeStrip)
};

} // namespace synth::ui
