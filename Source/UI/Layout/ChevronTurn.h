#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

// ChevronTurn (docs/layout/animation.md#fold-chevrons): the turn of a fold arrow. It holds the arrow's openness (0
// folded, 1 open, the number paintDisclosureChevron takes) and tweens it, 160 ms ease-out, retargeting from the
// CURRENT openness so a second click mid-turn reverses from where the arrow is. A turn is movement, so under Reduce
// Motion, with Animations Off, or while the owner is not on screen it lands at once (the header's rows still fade).
// The owner repaints on every frame; frames run only while the arrow turns.
namespace synth::ui {

class ChevronTurn {
public:
    static constexpr double kTurnMs = 160.0;

    explicit ChevronTurn(juce::Component& owner, bool open = true)
        : owner_(owner)
        , updater_(&owner)
        , openness_(open ? 1.0f : 0.0f)
        , target_(openness_) {
        registry().push_back(this);
    }

    ~ChevronTurn() {
        driver_.stop(updater_);
        registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end());
    }

    ChevronTurn(const ChevronTurn&) = delete;
    ChevronTurn& operator=(const ChevronTurn&) = delete;

    /** Turns to open or folded. A repeat of the current target does nothing. */
    void setOpen(bool open) {
        const float target = open ? 1.0f : 0.0f;
        if (target == target_)
            return;
        target_ = target;
        if (!canAnimate()) {
            land();
            return;
        }
        from_ = openness_;
        driver_.start(updater_, kTurnMs, easeOutCubic, [this](float t) { frame(t); }, [this] { land(); });
    }

    /** Lands on open or folded at once, stopping any turn (for the first layout of a header that was there from the
     *  start). */
    void snapTo(bool open) {
        target_ = open ? 1.0f : 0.0f;
        land();
    }

    /** The openness to paint: 0 folded, 1 open, in between mid-turn. */
    float openness() const noexcept { return openness_; }
    bool isTurning() const noexcept { return driver_.isRunning(); }
    bool isOpen() const noexcept { return target_ > 0.5f; }

    /** Test seam: steps every turn in flight by hand (t is the eased progress 0..1; 1 finishes them), since no VBlank
     *  reaches an off-screen component. Pair it with FadeVisibility::setAnimateOffScreenForTest. */
    static void stepAllForTest(float t) {
        const auto all = registry();
        for (auto* turn : all) {
            if (std::find(registry().begin(), registry().end(), turn) == registry().end() || !turn->driver_.isRunning())
                continue;
            if (t >= 1.0f)
                turn->land();
            else
                turn->frame(t);
        }
    }

private:
    static std::vector<ChevronTurn*>& registry() {
        static std::vector<ChevronTurn*> instances;
        return instances;
    }

    bool canAnimate() const { return FadeVisibility::canAnimateIn(&owner_) && !prefersReducedMotion(); }

    void frame(float t) {
        openness_ = from_ + (target_ - from_) * t;
        owner_.repaint();
    }

    void land() {
        driver_.stop(updater_);
        openness_ = target_;
        owner_.repaint();
    }

    juce::Component& owner_;
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
    float openness_;
    float target_;
    float from_ = 0.0f;
};

} // namespace synth::ui
