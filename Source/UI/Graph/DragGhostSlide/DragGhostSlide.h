#pragma once

// DragGhostSlide.h (docs/layout/insert-motion.md#the-hovered-module-slides-to-the-gap): the landing ghost of a module
// drag slides to the spot it is aimed at instead of jumping there. The ghost follows the pointer a grid step at a
// time, so small moves are followed one to one; a bigger jump (the ghost taking the gap a drag opened between two
// cards, or dodging to a free slot) is a slide of 160 ms, easeOutCubic, retargeted from where the ghost is drawn if
// the aim changes again. The state is pure (a test steps it with applyTweenAt); aim() also runs the driver.

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_graphics/juce_graphics.h>

class DragGhostSlide {
public:
    static constexpr double kSlideMs = 160.0;
    /** A move of the target up to this far on both axes is followed at once (a grid step of the pointer). */
    static constexpr int kFollowPx = 16;

    /** The ghost is now aimed at `target` (empty when there is no ghost). Slides there when the canvas `isShowing`
     *  (or a test forces it) and neither Reduce Motion nor Animations: Off is on, else lands at once. `repaint` gets
     *  the canvas area to redraw on every change. */
    void aim(juce::Rectangle<int> target, bool isShowing, juce::VBlankAnimatorUpdater& updater,
             const std::function<void(juce::Rectangle<int>)>& repaint);
    /** The drag begins or ends: forgets the ghost and stops a slide. */
    void reset(juce::VBlankAnimatorUpdater& updater);

    /** The ghost as drawn now. */
    juce::Rectangle<float> drawn() const noexcept;
    /** Drawn minus target position: how far the ghost still has to go (zero when it is not sliding). */
    juce::Point<float> offset() const noexcept;
    bool isSliding() const noexcept { return sliding_; }

    /** Test seams: the slide runs although the canvas is not showing; one driver frame at progress `t`. */
    void setForceForTest(bool force) noexcept { force_ = force; }
    void applyTweenAt(float t) noexcept;

private:
    bool retarget(juce::Rectangle<int> target, bool animate) noexcept;

    juce::Rectangle<int> to_;
    juce::Rectangle<float> from_;
    float progress_ = 1.0f;
    bool sliding_ = false;
    bool force_ = false;
    synth::ui::AnimationDriver driver_;
    std::function<void(juce::Rectangle<int>)> repaint_;
};
