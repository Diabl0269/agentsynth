#pragma once

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

// CalloutReveal.h (docs/layout/animation.md#popup-reveal): the entrance of a popover that opens in a juce::CallOutBox.
// The popup content owns one and calls startIfInCallout() when it gains a parent; the callout then fades in over
// 160 ms (easeOutCubic) while sliding a few pixels out of the side of the element that opened it, and lands exactly on
// its final bounds. It lands at once, with no motion, when the OS asks for reduced motion or the callout is not on
// screen. Only the entrance is animated: a callout dismisses synchronously, so there is no exit to ease.
namespace synth::ui {

class CalloutReveal {
public:
    static constexpr double kInMs = 160.0;
    static constexpr int kRisePx = 8;

    /** `content` is the popup; it must be the callout's content component. */
    explicit CalloutReveal(juce::Component& content);
    ~CalloutReveal();

    /** Call from the content's parentHierarchyChanged(). A no-op unless the content now sits in a CallOutBox. */
    void startIfInCallout();

    /** Pure: the unit step from the content's insets inside its callout towards the anchor the callout points at.
     *  The arrow side carries the largest inset (the arrow plus the border); a tie in a callout with no arrow is (0,
     * 0). */
    static juce::Point<int> directionToAnchor(juce::BorderSize<int> insets);

    /** Pure: the callout's alpha and its offset from the final position at eased progress `t` in [0, 1], for a reveal
     *  that starts `startOffset` away from where it ends. */
    struct Frame {
        float alpha = 1.0f;
        juce::Point<int> offset;
    };
    static Frame frameAt(float t, juce::Point<int> startOffset);

    bool isRunning() const noexcept { return driver_.isRunning(); }

    /** Test seam: applies one frame to the callout the content sits in, as the animation would; false when it sits in
     * none. */
    bool applyFrameForTest(float t);

private:
    juce::CallOutBox* findCallout() const;
    void begin(juce::Component::SafePointer<juce::CallOutBox> callout);
    void applyFrame(juce::CallOutBox& callout, float t);

    juce::Component& content_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    juce::Point<int> startOffset_;
    juce::Point<int> finalTopLeft_;
    bool pending_ = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CalloutReveal)
};

} // namespace synth::ui
