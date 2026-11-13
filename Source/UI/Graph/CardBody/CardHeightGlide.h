#pragma once

// CardHeightGlide (docs/layout/animation.md#fading-things-in-and-out, "Macros count"): a card whose measured height
// changes in one go (the Macros bank's Knobs count) draws that change as a glide instead of a snap. The layout stays
// final: the card measures itself at its new height as always, and this object only holds the DRAWN height back and
// eases it to the measured one (160 ms `easeOutCubic`, 80 ms under Reduce Motion, at once under Animations Off or when
// the card is not on screen). A measure that arrives mid-glide with another height continues from the drawn one.
// The neighbours are made room for once, by the owner (a growing card asks for its final footprint when the glide
// starts, a shrinking one gives the room back when it has landed: `settled`), never per frame.
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <utility>

namespace synth {

class CardHeightGlide {
public:
    static constexpr double kMs = 160.0;
    static constexpr double kReducedMs = 80.0;

    explicit CardHeightGlide(juce::Component& card)
        : card_(card)
        , updater_(&card) {}
    ~CardHeightGlide() { driver_.stop(updater_); }

    /** True when a change would be drawn as a glide now (the card is on screen and Animations is not Off). */
    bool canGlide() const { return ui::FadeVisibility::canAnimateIn(card_.getParentComponent()); }
    bool isGliding() const noexcept { return driver_.isRunning(); }
    int drawnHeight() const noexcept { return drawn_; }
    /** The height the running glide is easing to (the last measure). */
    int targetHeight() const noexcept { return to_; }

    /** Starts drawing the card at `from` and easing to `to`; the caller has measured the card at `to` and asked
     *  the neighbours for the room a growth needs. `settled` runs once when the glide has landed. */
    void begin(int from, int to, std::function<void()> settled) {
        settled_ = std::move(settled);
        run(from, to);
    }

    /** The height to give the card for a fresh measure of `measured`: the drawn height while a glide runs (a
     *  different measure re-aims the glide from where it is), the measure otherwise. */
    int heightFor(int measured) {
        if (!isGliding())
            return measured;
        if (measured != to_)
            run(drawn_, measured);
        return drawn_;
    }

    /** Test seam: advances the running glide to `ms` after it began (no VBlank reaches an off-screen card). */
    void stepForTest(double ms) { driver_.stepForTest(ms); }

private:
    void run(int from, int to) {
        from_ = from;
        to_ = to;
        drawn_ = from;
        card_.setSize(card_.getWidth(), from);
        const double ms = ui::motionMs(kMs, kReducedMs);
        const bool reduced = ui::prefersReducedMotion();
        driver_.start(
            updater_, std::max(1.0, ms), [reduced](float t) { return reduced ? t : ui::easeOutCubic(t); },
            [this](float e) {
                drawn_ = from_ + static_cast<int>(std::lround(static_cast<double>(to_ - from_) * e));
                card_.setSize(card_.getWidth(), drawn_);
            },
            [this] {
                drawn_ = to_;
                card_.setSize(card_.getWidth(), to_);
                if (auto done = std::exchange(settled_, {}))
                    done();
            });
    }

    juce::Component& card_;
    juce::VBlankAnimatorUpdater updater_;
    ui::AnimationDriver driver_;
    std::function<void()> settled_;
    int from_ = 0, to_ = 0, drawn_ = 0;
};

} // namespace synth
