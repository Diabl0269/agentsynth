#pragma once

#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <functional>
#include <initializer_list>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// FadeVisibility (docs/layout/animation.md#fading-things-in-and-out): shows and hides one or more child
// components with the app's soft fade instead of a pop. 160 ms in, 110 ms out, linear in time (the same
// numbers as popups and tooltips); a plain 80 ms fade under Reduce Motion; no time at all under Off.
//
// - Hiding keeps the components visible until the fade has ended, then hides them (and puts their alpha back
//   to 1, so a plain setVisible(true) elsewhere still shows them whole).
// - Showing makes them visible at alpha 0 first, so frame 0 is already the first fade frame.
// - A reversal mid-fade starts from the CURRENT opacity.
// - Nothing is animated while the parent is not on screen (a headless test, a hidden panel): the change lands
//   synchronously and `onFrame` is NOT called, so a caller that lays out after calling setShown() is correct
//   either way.
// - While the fade-out runs the components ignore the mouse, so a button cannot be clicked twice.
//
// `progress()` is the shown amount, 0 to 1, for an owner that also tweens a height with it (a banner strip):
// pass `onFrame` and lay out again from it. State is read back from the components (visible, alpha), so a
// direct setVisible() elsewhere never leaves it out of step.
namespace synth::ui {

class FadeVisibility {
public:
    static constexpr double kFadeInMs = 160.0;
    static constexpr double kFadeOutMs = 110.0;
    static constexpr double kReducedMs = 80.0;

    FadeVisibility(std::initializer_list<juce::Component*> targets)
        : targets_(targets)
        , updater_(targets_.front()) {
        registry().push_back(this);
    }

    ~FadeVisibility() {
        driver_.stop(updater_);
        registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end());
    }

    FadeVisibility(const FadeVisibility&) = delete;
    FadeVisibility& operator=(const FadeVisibility&) = delete;

    /** Called each frame of a running fade (including the first and the last). */
    std::function<void()> onFrame;
    /** Called once when a fade-out has finished and the components are hidden (not for a synchronous hide). */
    std::function<void()> onHidden;

    /** Fade the components in or out. A repeat of the current direction does nothing. */
    void setShown(bool shown) {
        if (shown == isShown())
            return; // already there, or already fading that way

        if (!canAnimate()) {
            land(shown);
            return;
        }

        from_ = currentProgress();
        to_ = shown ? 1.0f : 0.0f;
        fadingOut_ = !shown;
        blockInput(!shown);
        apply(from_);
        for (auto* c : targets_)
            c->setVisible(true);
        const double ms = shown ? motionMs(kFadeInMs, kReducedMs) : motionMs(kFadeOutMs, kReducedMs);
        driver_.start(updater_, ms, [](float t) { return t; }, [this](float t) { frame(t); }, [this] { finish(); });
    }

    /** True while the components are shown or fading in: the answer to "is it logically on". */
    bool isShown() const { return isVisibleNow() && !fadingOut_; }

    /** True while a fade is running. */
    bool isFading() const noexcept { return driver_.isRunning(); }

    /** The shown amount: 0 hidden, 1 fully shown, in between mid-fade. */
    float progress() const { return currentProgress(); }

    /** Test seams: animate even though the parent is not on screen, and step every running fade by hand
     *  (t in 0..1; 1 finishes them) since no VBlank reaches an off-screen component. */
    static void setAnimateOffScreenForTest(bool animate) { forceAnimate() = animate; }
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
    bool isVisibleNow() const { return targets_.front()->isVisible(); }

    float currentProgress() const {
        if (driver_.isRunning())
            return progress_;
        return isVisibleNow() ? 1.0f : 0.0f;
    }

    bool canAnimate() const { return canAnimateIn(targets_.front()->getParentComponent()); }

public:
    /** Whether a change inside `host` should animate: it is on screen (no VBlank reaches anything else) and
     *  Animations is not Off. Also what the status bar asks of itself. */
    static bool canAnimateIn(const juce::Component* host) {
        return (forceAnimate() || (host != nullptr && host->isShowing())) && !animationsOff();
    }

private:
    static std::vector<FadeVisibility*>& registry() {
        static std::vector<FadeVisibility*> instances;
        return instances;
    }

    static bool& forceAnimate() {
        static bool value = false;
        return value;
    }

    void frame(float t) {
        apply(from_ + (to_ - from_) * t);
        if (onFrame)
            onFrame();
    }

    // The end of a fade. Stopping the driver destroys the callback that got here, so nothing is read from it.
    void finish() {
        const bool wasFadeOut = fadingOut_;
        const auto frameCallback = onFrame;
        const auto hidden = onHidden;
        land(to_ > 0.5f);
        if (frameCallback)
            frameCallback();
        if (wasFadeOut && hidden)
            hidden();
    }

    void apply(float p) {
        progress_ = juce::jlimit(0.0f, 1.0f, p);
        for (auto* c : targets_)
            c->setAlpha(progress_);
    }

    // The final state, synchronously.
    void land(bool shown) {
        driver_.stop(updater_);
        fadingOut_ = false;
        progress_ = shown ? 1.0f : 0.0f;
        blockInput(false);
        for (auto* c : targets_) {
            c->setAlpha(1.0f);
            c->setVisible(shown);
        }
    }

    void blockInput(bool block) {
        if (block == blocked_)
            return;
        blocked_ = block;
        if (block) {
            saved_.clear();
            for (auto* c : targets_) {
                bool self = true;
                bool kids = true;
                c->getInterceptsMouseClicks(self, kids);
                saved_.push_back({self, kids});
                c->setInterceptsMouseClicks(false, false);
                // a leaving control never keeps the keyboard
                if (c->hasKeyboardFocus(true))
                    c->giveAwayKeyboardFocus();
            }
        } else {
            for (size_t i = 0; i < targets_.size() && i < saved_.size(); ++i)
                targets_[i]->setInterceptsMouseClicks(saved_[i].first, saved_[i].second);
        }
    }

    std::vector<juce::Component*> targets_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    float progress_ = 1.0f;
    float from_ = 0.0f;
    float to_ = 1.0f;
    bool fadingOut_ = false;
    bool blocked_ = false;
    std::vector<std::pair<bool, bool>> saved_;
};

} // namespace synth::ui
