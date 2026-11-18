#include "UsageStatsCardSlot.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

namespace {
constexpr double kCollapseMs = 180.0;    // leaving: easeInCubic
constexpr double kThanksHoldMs = 1100.0; // Share's one-line thanks, faded over its last third
} // namespace

UsageStatsCardSlot::UsageStatsCardSlot(juce::Component& owner, std::function<void()> relayout)
    : owner_(owner)
    , relayout_(std::move(relayout))
    , vblank_(&owner) {
    prompt_.setVisible(false);
    prompt_.onChoice = [this](bool share) { handleChoice(share); };
    prompt_.onLearnMoreRequested = [this] {
        if (onLearnMore)
            onLearnMore();
    };
}

int UsageStatsCardSlot::slotHeight() const {
    return juce::roundToInt((float)(UsageStatsPromptComponent::kHeight + kGap) * fraction_);
}

void UsageStatsCardSlot::layoutSlot(juce::Rectangle<int>& area) {
    if (const int slot = slotHeight(); slot > 0) {
        const int gapPart = juce::roundToInt((float)kGap * fraction_);
        prompt_.setBounds(area.removeFromTop(slot).withTrimmedBottom(gapPart));
    }
}

void UsageStatsCardSlot::setShown(bool shown) {
    if (shown && state_ == State::hidden) {
        state_ = State::asking;
        fraction_ = 1.0f;
        prompt_.setAlpha(1.0f);
        prompt_.setVisible(true);
        relayout_();
    } else if (!shown && state_ == State::asking) {
        state_ = State::answered;
        collapse();
    }
}

void UsageStatsCardSlot::handleChoice(bool share) {
    if (state_ != State::asking)
        return;
    state_ = State::answered; // a second click while the thanks shows does nothing
    if (onChoice)
        onChoice(share);
    // The pressed button is about to go; a keyboard user lands on something that stays instead of nowhere.
    if (prompt_.hasKeyboardFocus(true) && onFocusRescue)
        onFocusRescue();

    const bool animate = owner_.isShowing() && !synth::ui::prefersReducedMotion();
    if (!share || !animate) {
        collapse();
        return;
    }
    // Share only: the buttons give way to a thanks that holds, then fades, then the card collapses.
    prompt_.showConfirmation(true);
    anim_.start(
        vblank_, kThanksHoldMs, [](float t) { return t; },
        [this](float t) { prompt_.setConfirmationAlpha(t < 0.67f ? 1.0f : 1.0f - (t - 0.67f) / 0.33f); },
        [safe = juce::Component::SafePointer<juce::Component>(&owner_), this] {
            // Not collapsed from inside this animator's own completion: starting a driver stops the running one.
            juce::MessageManager::callAsync([safe, this] {
                if (safe != nullptr)
                    collapse();
            });
        });
}

void UsageStatsCardSlot::collapse() {
    if (!owner_.isShowing() || synth::ui::prefersReducedMotion()) {
        anim_.stop(vblank_);
        finishCollapse();
        return;
    }
    anim_.start(
        vblank_, kCollapseMs, synth::ui::easeInCubic,
        [this](float e) {
            fraction_ = 1.0f - e;
            prompt_.setAlpha(fraction_);
            relayout_();
        },
        [this] { finishCollapse(); });
}

void UsageStatsCardSlot::finishCollapse() {
    fraction_ = 0.0f;
    prompt_.setVisible(false);
    prompt_.showConfirmation(false);
    prompt_.setAlpha(1.0f);
    relayout_();
}

} // namespace synth::ui
