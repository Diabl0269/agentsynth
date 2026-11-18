#pragma once

#include "UI/Layout/HelperTooltip.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

// AppTooltipWindow.h (docs/layout/animation.md#tooltips): the ONE tooltip window every app window uses (the main
// window and each detached panel window own one). A juce::TooltipWindow that
//   * fades in when a tip appears (160 ms, easeOutCubic) and out when it goes (110 ms, easeInCubic), the numbers every
//     popup shares (popup_motion in PopupMotion.h); under Reduce Motion it is the popups' plain 80 ms fade;
//   * hides ordinary (info) tooltips when Preferences > "Show info tooltips" is off, but still shows helper tips
//     (HelperTooltip.h);
//   * keeps the tip off a row whose component sets tooltip_anchor::kKeepClearProperty (TooltipAnchor.h): just below it
//   at the
//     pointer's x, above it near the bottom of the window; every other component keeps juce's placement.
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

    /** Component property (the control's name): its name is drawn beside it, so the hover tip leaves the name out and
     *  keeps only what follows it (a MIDI mapping, "Automated: ..."); a tip that is only the name shows nothing. */
    static inline const juce::Identifier kNameDrawnBeside{"nameDrawnBeside"};
    static juce::String withoutDrawnName(const juce::Component& c, const juce::String& tip);

    /** While anything has hover tips blocked, getTipFor answers empty, so no tip appears and one showing goes. Counted:
     *  every block(true) needs its block(false). The mod dot's readout blocks them while it is up, so one gesture
     *  never shows two readouts. */
    static void blockHoverTips(bool block) noexcept;
    static bool hoverTipsBlocked() noexcept;

    // juce places a tip at the OS cursor, which synthetic input never moves; while test mode has set the synthetic
    // pointer (UI/Layout/PointerState.h) every tip is placed at that point instead.

    /** Where the pointer is in `c`'s own coordinates: the synthetic pointer under test mode, else the real one. A
     *  TooltipClient whose tip depends on where it is hovered (blocks in a row) asks this in getTooltip, since
     *  getMouseXYRelative reads the OS cursor, which test-mode input never moves. */
    static juce::Point<float> pointerIn(const juce::Component& c);

    /** Test mode: `tipText` (empty when no tip shows) and `tipAtPointer` (true when no tip shows, else whether the tip
     *  sits within a short reach of the synthetic pointer, as a tip beside the control under it does). */
    void addTestModeState(juce::DynamicObject& into) const;

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
    /** Stands in for the window the tip lives in being on screen, while the tip itself is hidden: the state a tip is
     *  in when juce has just hidden it and the fade-out has to start. */
    void setHostShowingForTest(bool showing) noexcept { hostShowingForTest_ = showing; }
    void applyFadeInFrameForTest(float eased);
    bool hasLeavingGhostForTest() const noexcept;
    /** Stands in for the tip getTipFor last returned, which is the text the leaving ghost paints. */
    void setLastTipForTest(const juce::String& tip) { lastTip_ = tip; }
    /** Stands in for the component under the pointer that getTipFor last asked about (the one whose anchor places the
     *  tip). */
    void setTipComponentForTest(juce::Component* c) { tipComponent_ = c; }

private:
    class Ghost;
    class StateChild;

    void visibilityChanged() override;
    void moved() override;
    void placeAtSyntheticPointer();
    void placeBesideAnchor();
    bool shouldAnimate() const;
    void beginFadeIn();
    void beginFadeOut();

    juce::ApplicationProperties* appProperties_ = nullptr;
    juce::Component::SafePointer<juce::Component> tipComponent_; // the component the last getTipFor asked about
    juce::String lastTip_; // what juce is (or was last) showing: the ghost paints it, since juce clears its own copy
    bool animateOffScreen_ = false;
    bool hostShowingForTest_ = false;
    bool fadingIn_ = false;
    bool destroying_ = false;
    bool replacing_ = false;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    std::unique_ptr<Ghost> ghost_;
    std::unique_ptr<StateChild>
        stateChild_; // the id test mode reads (a sibling, so this header stays free of test mode)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AppTooltipWindow)
};

} // namespace synth::ui
