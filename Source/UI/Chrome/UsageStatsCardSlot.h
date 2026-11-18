#pragma once

#include "UI/Chrome/UsageStatsPromptComponent.h"
#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The usage statistics question in a slot of its owner's layout, with its whole life: open while the question is
// unanswered, a one-line thanks after Share (held about a second, faded over its last third), then a collapse (height
// and fade, 180 ms, easeInCubic) that gives the slot's height back to the owner's layout. Reduced Motion and an owner
// that is not on screen land at once. Used by the Welcome screen and by the top of Settings > Preferences, so both
// places ask the same question the same way (docs/development/usage-statistics.md). The owner decides what an
// answer writes (onChoice) and what "What we collect" opens (onLearnMore); the slot never touches settings.
//
// The owner adds prompt() as a child itself (so it picks the Tab-order position it wants), calls layoutSlot() from
// its resized(), and passes a `relayout` that runs that resized() again for each animation frame.
class UsageStatsCardSlot {
public:
    /** The gap the slot adds under the panel, shrinking with it. */
    static constexpr int kGap = 14;

    UsageStatsCardSlot(juce::Component& owner, std::function<void()> relayout);

    UsageStatsPromptComponent& prompt() { return prompt_; }

    /** Share (true) / No thanks (false). Runs before the collapse starts. */
    std::function<void(bool share)> onChoice;
    std::function<void()> onLearnMore;
    /** The pressed button is about to go while it had keyboard focus: the owner moves focus somewhere that stays. */
    std::function<void()> onFocusRescue;

    /** true opens the card at once (it is part of the layout) unless it was answered; false collapses it out when
     *  it is open and idle. Ignored while an answer is being animated. */
    void setShown(bool shown);
    /** On screen right now: open and unanswered, or still animating out. */
    bool isShown() const noexcept { return state_ == State::asking || fraction_ > 0.0f; }
    /** The height the slot takes from the layout right now: panel plus gap, times how open it is (0 once gone). */
    int slotHeight() const;
    /** Takes the slot off the top of `area` and places the panel in it (gap kept below). Nothing when closed. */
    void layoutSlot(juce::Rectangle<int>& area);

private:
    enum class State { hidden, asking, answered };
    void handleChoice(bool share);
    void collapse();
    void finishCollapse();

    juce::Component& owner_;
    std::function<void()> relayout_;
    UsageStatsPromptComponent prompt_;
    State state_ = State::hidden;
    float fraction_ = 0.0f; // 1 = fully open, 0 = gone; the layout reads only this
    juce::VBlankAnimatorUpdater vblank_;
    AnimationDriver anim_;
};

} // namespace synth::ui
