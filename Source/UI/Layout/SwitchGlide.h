#pragma once

#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <vector>

// SwitchGlide (docs/layout/theming.md#themed-widgets): the one on/off switch the app draws by hand (a pill with a
// round knob), and the glide between its two states. The knob slides across and the pill's fill fades, instead of the
// knob popping to the other side. The owner (a row, a checkbox) holds one, calls retarget() when its state changes and
// paint() from its own paint; the colours are passed in so the painter knows no owner's types.
//
// 110 ms in (easeOutCubic, switching on), 90 ms out (easeInCubic, switching off), the same pair as ButtonStateEase;
// 80 ms under Reduce Motion, none under Animations Off. A change the owner says should not animate (its first bind, a
// different entry bound to the same row), and any change while the owner is not on screen, lands at once. Frames run
// only while a tween runs and repaint only the owner.
namespace synth::ui {

class SwitchGlide final {
public:
    static constexpr int kWidth = 26;
    static constexpr int kHeight = 14;
    static constexpr int kGapAfter = 8;
    static constexpr double kInMs = 110.0;
    static constexpr double kOutMs = 90.0;

    struct Colours {
        juce::Colour onFill;  // the pill when on (it fades out into the outline when off)
        juce::Colour offLine; // the outline and knob when off
        juce::Colour onKnob;  // the knob when on
    };

    explicit SwitchGlide(juce::Component& owner)
        : owner_(owner) {
        registry().push_back(this);
    }
    SwitchGlide(const SwitchGlide&) = delete;
    SwitchGlide& operator=(const SwitchGlide&) = delete;
    ~SwitchGlide() {
        registry().erase(std::remove(registry().begin(), registry().end(), this), registry().end());
        driver_.stop();
    }

    /** Takes the switch's place off the right edge of `content` (the content loses it and a gap) and returns it. */
    static juce::Rectangle<int> takeFromRight(juce::Rectangle<int>& content) {
        const auto pill = content.removeFromRight(kWidth).withSizeKeepingCentre(kWidth, kHeight);
        content.removeFromRight(kGapAfter);
        return pill;
    }

    /** The knob's left edge in a pill at `amount` (0 off, 1 on). */
    static float knobX(juce::Rectangle<float> pill, float amount) {
        const float knob = pill.getHeight() - 4.0f;
        const float offX = pill.getX() + 2.0f;
        const float onX = pill.getRight() - 2.0f - knob;
        return offX + (onX - offX) * amount;
    }

    /** Points the switch at `on`. `animate` false (or an owner off screen, or Animations Off) lands at once. */
    void retarget(bool on, bool animate) {
        const float target = on ? 1.0f : 0.0f;
        if (!animate || !FadeVisibility::canAnimateIn(&owner_)) {
            const bool moved = now_ != target;
            to_ = target;
            settle();
            if (moved)
                owner_.repaint();
            return;
        }
        if (target == to_)
            return;
        const float previous = now_;
        to_ = target;
        from_ = previous;
        if (updater_ == nullptr)
            updater_ = std::make_unique<juce::VBlankAnimatorUpdater>(&owner_);
        const bool in = on;
        driver_.start(
            *updater_, motionMs(in ? kInMs : kOutMs, FadeVisibility::kReducedMs),
            [in](float t) { return in ? easeOutCubic(t) : easeInCubic(t); }, [this](float t) { frame(t); },
            [this] { land(); });
    }

    /** Draws the switch in `pill` at the current amount. */
    void paint(juce::Graphics& g, juce::Rectangle<float> pill, const Colours& colours) const {
        const float radius = pill.getHeight() * 0.5f;
        if (now_ > 0.0f) {
            g.setColour(colours.onFill.withMultipliedAlpha(now_));
            g.fillRoundedRectangle(pill, radius);
        }
        if (now_ < 1.0f) {
            g.setColour(colours.offLine.withMultipliedAlpha(1.0f - now_));
            g.drawRoundedRectangle(pill.reduced(0.5f), radius, 1.0f);
        }
        const float knob = pill.getHeight() - 4.0f;
        g.setColour(colours.offLine.interpolatedWith(colours.onKnob, now_));
        g.fillEllipse(knobX(pill, now_), pill.getY() + 2.0f, knob, knob);
    }

    /** How far the switch has eased: 0 off, 1 on. */
    float amount() const noexcept { return now_; }
    bool isEasing() const noexcept { return driver_.isRunning(); }

    /** Test seam: steps every glide in flight by hand (t in 0..1; 1 finishes them). */
    static void stepAllForTest(float t) {
        const auto all = registry();
        for (auto* s : all) {
            if (std::find(registry().begin(), registry().end(), s) == registry().end() || !s->driver_.isRunning())
                continue;
            if (t >= 1.0f)
                s->land();
            else
                s->frame(t);
        }
    }

private:
    static std::vector<SwitchGlide*>& registry() {
        static std::vector<SwitchGlide*> instances;
        return instances;
    }

    void frame(float t) {
        now_ = from_ + (to_ - from_) * t;
        owner_.repaint();
    }

    // The end of a tween. Stopping the driver destroys the callback that got here, so nothing is read from it after.
    void land() {
        settle();
        owner_.repaint();
    }

    void settle() {
        driver_.stop();
        now_ = to_;
    }

    juce::Component& owner_;
    std::unique_ptr<juce::VBlankAnimatorUpdater> updater_;
    AnimationDriver driver_;
    float from_ = 0.0f, to_ = 0.0f, now_ = 0.0f;
};

} // namespace synth::ui
