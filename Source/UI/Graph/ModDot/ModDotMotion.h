#pragma once

// The motion helpers the mod dot's panel shares: whether to animate at all, and the 100 ms hover fade of a row
// or button. Nothing animates headless, off screen, or under the OS's reduced-motion setting: it lands at once.
// docs/layout/animation.md#motion-rules.

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

inline bool modDotMotionAllowed(const juce::Component& c) { return c.isShowing() && !prefersReducedMotion(); }

/** The 0..1 hover highlight of one component, eased over 100 ms (retargets from where it is). */
class ModDotHoverFade {
public:
    static constexpr double kMs = 100.0;

    explicit ModDotHoverFade(juce::Component& owner)
        : owner_(owner)
        , updater_(&owner) {}
    ~ModDotHoverFade() { driver_.stop(updater_); }

    void setHovered(bool hovered) {
        const float target = hovered ? 1.0f : 0.0f;
        if (target == target_)
            return;
        target_ = target;
        if (!modDotMotionAllowed(owner_)) {
            driver_.stop(updater_);
            value_ = target;
            owner_.repaint();
            return;
        }
        const float from = value_;
        driver_.start(updater_, kMs, easeOutCubic, [this, from, target](float t) {
            value_ = from + (target - from) * t;
            owner_.repaint();
        });
    }
    float value() const noexcept { return value_; }

private:
    juce::Component& owner_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    float value_ = 0.0f;
    float target_ = 0.0f;
};

} // namespace synth::ui
