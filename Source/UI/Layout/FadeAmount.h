#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// FadeAmount (docs/layout/animation.md#fading-things-in-and-out): the fade of something its owner PAINTS (a hint, an
// outline), where there is no component of its own to fade. The owner multiplies its colours by `value()` and
// repaints from `onFrame`. Same numbers as FadeVisibility (160 ms in, 110 ms out, linear in time, 80 ms under Reduce
// Motion, none under Animations Off), a reversal starts from the CURRENT amount, and nothing animates while the owner
// is not on screen (the change lands before setShown() returns). Frames run only while a fade runs.
namespace synth::ui {

class FadeAmount {
public:
    explicit FadeAmount(juce::Component& owner)
        : owner_(owner)
        , updater_(&owner) {
        registry().push_back(this);
    }

    ~FadeAmount() {
        driver_.stop(updater_);
        registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end());
    }

    FadeAmount(const FadeAmount&) = delete;
    FadeAmount& operator=(const FadeAmount&) = delete;

    /** Called on every frame of a fade (and the owner is repainted). */
    std::function<void()> onFrame;
    /** Called once when a fade has finished (not for a change that landed at once). */
    std::function<void()> onSettled;
    /** The part of the owner (its own coordinates) that a frame repaints; unset repaints the whole owner. A large owner
     *  with a small painted thing names the thing's area, so a frame never repaints more than it moves. */
    std::function<juce::Rectangle<int>()> repaintArea;

    /** Fades in or out. A repeat of the current direction does nothing. */
    void setShown(bool shown) {
        const float target = shown ? 1.0f : 0.0f;
        if (target == target_)
            return;
        target_ = target;
        if (!FadeVisibility::canAnimateIn(&owner_)) {
            land();
            return;
        }
        from_ = value_;
        driver_.start(
            updater_,
            shown ? motionMs(FadeVisibility::kFadeInMs, FadeVisibility::kReducedMs)
                  : motionMs(FadeVisibility::kFadeOutMs, FadeVisibility::kReducedMs),
            [](float t) { return t; }, [this](float t) { frame(t); }, [this] { finish(); });
    }

    /** Lands on shown or hidden at once. */
    void snapTo(bool shown) {
        target_ = shown ? 1.0f : 0.0f;
        land();
    }

    /** The amount to paint with: 0 hidden, 1 fully shown. */
    float value() const noexcept { return value_; }
    /** True while shown or fading in: the logical answer. */
    bool isShown() const noexcept { return target_ > 0.5f; }
    bool isFading() const noexcept { return driver_.isRunning(); }

    /** Test seam: steps every fade in flight by hand (t in 0..1; 1 finishes them). */
    static void stepAllForTest(float t) {
        const auto all = registry();
        for (auto* fade : all) {
            if (std::find(registry().begin(), registry().end(), fade) == registry().end() || !fade->driver_.isRunning())
                continue;
            if (t >= 1.0f)
                fade->finish();
            else
                fade->frame(t);
        }
    }

private:
    static std::vector<FadeAmount*>& registry() {
        static std::vector<FadeAmount*> instances;
        return instances;
    }

    void frame(float t) {
        value_ = from_ + (target_ - from_) * t;
        repaintOwner();
        if (onFrame)
            onFrame();
    }

    // The end of a fade. Stopping the driver destroys the callback that got here, so nothing is read from it.
    void finish() {
        const auto frameCallback = onFrame;
        const auto settled = onSettled;
        land();
        if (frameCallback)
            frameCallback();
        if (settled)
            settled();
    }

    void land() {
        driver_.stop(updater_);
        value_ = target_;
        repaintOwner();
    }

    void repaintOwner() {
        if (repaintArea)
            owner_.repaint(repaintArea());
        else
            owner_.repaint();
    }

    juce::Component& owner_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    float value_ = 0.0f;
    float target_ = 0.0f;
    float from_ = 0.0f;
};

} // namespace synth::ui
