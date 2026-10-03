#pragma once

#include "UI/Layout/HelperTooltip.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

// AppTooltipWindow.h (docs/layout/animation.md#tooltips): the ONE tooltip window every app window uses (the main
// window and each detached panel window own one). A juce::TooltipWindow that
//   * fades in when a tip appears (160 ms, easeOutCubic) and out when it goes (110 ms, easeInCubic), the numbers every
//     popup shares (popup_motion in PopupMotion.h); under Reduce Motion it is the popups' plain 80 ms fade;
//   * hides ordinary (info) tooltips when Preferences > "Show info tooltips" is off, but still shows helper tips
//     (HelperTooltip.h).
// The window is a child of its app window, so it cannot use PopupMotion::attach (desktop windows only): it drives its
// own alpha. juce hides a tip synchronously, so the fade-out runs on a click-through ghost sibling.
namespace synth::ui {

/** The user-settings key of Preferences > "Show info tooltips" (a bool, default true). */
constexpr const char* kShowInfoTooltipsKey = "showInfoTooltips";

class AppTooltipWindow : public juce::TooltipWindow {
public:
    /** `parent`: the app window the tip lives in. `appProperties`: where the "Show info tooltips" preference is read
     *  from, each time a tip is asked for, so a toggle applies at once; may be null (info tooltips stay on). It must
     *  outlive this window. */
    explicit AppTooltipWindow(juce::Component* parent, juce::ApplicationProperties* appProperties = nullptr,
                              int millisecondsBeforeTipAppears = 700);
    ~AppTooltipWindow() override;

    /** juce's tip for `c`, or empty when it is an info tooltip and the preference is off. */
    juce::String getTipFor(juce::Component& c) override;

    /** The preference, read now: true when info tooltips are on (the default). */
    bool areInfoTooltipsOn() const;

    /** The decision getTipFor applies: true when `c`'s tooltip is an info tooltip (not marked helper) and info
     *  tooltips are off. */
    bool suppresses(const juce::Component& c) const;

    /** True while a fade is running (in, or the leaving ghost's out). */
    bool isFading() const noexcept;

    /** Test seams: headless windows are never "showing", so nothing would animate; this lets a test start the fade
     *  anyway and apply frames by hand (`eased` in [0, 1], 1 lands), as a VBlank would. */
    void setAnimateOffScreenForTest(bool animate) noexcept { animateOffScreen_ = animate; }
    void applyFadeInFrameForTest(float eased);
    bool hasLeavingGhostForTest() const noexcept;
    /** Stands in for the tip getTipFor last returned, which is the text the leaving ghost paints. */
    void setLastTipForTest(const juce::String& tip) { lastTip_ = tip; }

private:
    class Ghost;

    void visibilityChanged() override;
    bool shouldAnimate() const;
    void beginFadeIn();
    void beginFadeOut();

    juce::ApplicationProperties* appProperties_ = nullptr;
    juce::String lastTip_; // what juce is (or was last) showing: the ghost paints it, since juce clears its own copy
    bool animateOffScreen_ = false;
    bool fadingIn_ = false;
    bool destroying_ = false;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    std::unique_ptr<Ghost> ghost_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppTooltipWindow)
};

} // namespace synth::ui
