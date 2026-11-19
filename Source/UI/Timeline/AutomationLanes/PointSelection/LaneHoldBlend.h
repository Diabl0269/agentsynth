#pragma once

#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

// LaneHoldBlend -- the draw-only blend between a segment's curve and its step while a bend drag crosses into or out of
// Hold, so the switch never pops. amount() is 0 for the curve and 1 for the step; the owner draws the curve flattened
// that far onto the step's start value (one line morphing). A drag moving in small steps sets it directly
// (follow), a jump or a release eases there over kMs. The committed model never holds a blend.
//
// The owner must outlive it; it repaints the owner itself. Reduce Motion, Animations Off and a hidden owner land at
// once. Message thread only.
namespace synth::ui {

class LaneHoldBlend {
public:
    static constexpr double kMs = 240.0;

    explicit LaneHoldBlend(juce::Component& owner);
    ~LaneHoldBlend();

    // Sets where the blend rests with no animation (a new drag).
    void reset(bool hold);
    // Eases toward the step (true) or the curve (false); no change when already there.
    void setHold(bool hold);
    // Moves to `amount` (0..1): at once for a step of at most kFollowStep while nothing runs, else eased from where it
    // is, so a slow drag tracks the pointer and a jump still glides.
    void follow(float amount);
    static constexpr float kFollowStep = 0.25f;

    bool isRunning() const noexcept { return running_; }
    // 0 the curve .. 1 the step.
    float amount() const noexcept { return amount_; }

    // Tests: animate as if the owner were on screen, so no native window is needed.
    void forceAnimateForTest(bool force) noexcept { forceAnimateForTest_ = force; }

private:
    void easeTo(float to);
    bool landsAtOnce() const;

    juce::Component& owner_;
    float target_ = 0.0f;
    bool running_ = false;
    bool forceAnimateForTest_ = false;
    float amount_ = 0.0f;
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver driver_;
};

} // namespace synth::ui
