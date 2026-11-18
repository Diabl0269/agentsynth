#pragma once

// MacroJackFade (docs/layout/animation.md, "Macros count"): the output jacks of a Macros bank fade with its
// Controllers count instead of popping. A jack the count adds fades (and its name with it) in while its controller
// grows in; one it takes away stays drawn as a ghost, fades out and is then gone. Timing is the card height's glide:
// 160 ms `easeOutCubic`, 80 ms linear under Reduce Motion, at once under Animations Off or when the card is not on
// screen. A count that changes mid-fade continues from the alphas on screen. paint() asks `jackAlpha` per jack and
// draws `ghostCount` extra jacks past the count; it holds no geometry (a jack's place is the layout's, which is
// final at once).
#include "UI/Layout/FadeVisibility.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>
#include <utility>

namespace synth {

class MacroJackFade {
public:
    static constexpr double kMs = 160.0;
    static constexpr double kReducedMs = 80.0;

    MacroJackFade(juce::Component& card, int count)
        : card_(card)
        , updater_(&card)
        , count_(count) {}
    ~MacroJackFade() { driver_.stop(updater_); }

    /** The count the bank shows now: jacks past it that are still drawn are fading out. */
    void changeCount(int count) {
        if (count == count_)
            return;
        std::map<int, std::pair<float, float>> next;
        const int top = std::max({count_, count, fades_.empty() ? 0 : fades_.rbegin()->first + 1});
        for (int jack = 0; jack < top; ++jack) {
            const float now = jackAlpha(jack);
            const float aim = jack < count ? 1.0f : 0.0f;
            if (now != aim)
                next[jack] = {now, aim};
        }
        count_ = count;
        fades_ = std::move(next);
        e_ = 0.0f;
        if (fades_.empty() || !ui::FadeVisibility::canAnimateIn(card_.getParentComponent())) {
            land();
            return;
        }
        const double ms = ui::motionMs(kMs, kReducedMs);
        const bool reduced = ui::prefersReducedMotion();
        driver_.start(
            updater_, std::max(1.0, ms), [reduced](float t) { return reduced ? t : ui::easeOutCubic(t); },
            [this](float e) {
                e_ = e;
                card_.repaint();
            },
            [this] { land(); });
    }

    /** 0..1: how much of output jack `jack` (and its name) shows now. */
    float jackAlpha(int jack) const {
        if (const auto it = fades_.find(jack); it != fades_.end())
            return it->second.first + (it->second.second - it->second.first) * e_;
        return jack < count_ ? 1.0f : 0.0f;
    }
    /** How many jacks past the count are still drawn, fading out. */
    int ghostCount() const {
        int ghosts = 0;
        for (const auto& [jack, fade] : fades_)
            if (jack >= count_ && jackAlpha(jack) > 0.0f)
                ghosts = std::max(ghosts, jack + 1 - count_);
        return ghosts;
    }
    bool isFading() const noexcept { return driver_.isRunning(); }

    /** Set at the start of a count gesture; runs once after the change is laid out (the cables it took away retract).
     */
    std::function<void()> afterChange;

    /** Test seam: advances the running fade to `ms` after it began (no VBlank reaches an off-screen card). */
    void stepForTest(double ms) { driver_.stepForTest(ms); }

private:
    void land() {
        fades_.clear();
        e_ = 1.0f;
        card_.repaint();
    }

    juce::Component& card_;
    juce::VBlankAnimatorUpdater updater_;
    ui::AnimationDriver driver_;
    std::map<int, std::pair<float, float>> fades_; // jack -> alpha it started from, alpha it eases to
    int count_ = 0;
    float e_ = 1.0f;
};

} // namespace synth
