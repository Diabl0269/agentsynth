// DragGhostSlide.cpp -- see DragGhostSlide.h.

#include "DragGhostSlide.h"

#include "UI/Layout/ReducedMotion.h"
#include <cstdlib>

bool DragGhostSlide::retarget(juce::Rectangle<int> target, bool animate) noexcept {
    if (target.isEmpty()) {
        to_ = {};
        from_ = {};
        progress_ = 1.0f;
        sliding_ = false;
        return false;
    }
    if (to_.isEmpty() || !animate) {
        to_ = target;
        from_ = target.toFloat();
        progress_ = 1.0f;
        sliding_ = false;
        return false;
    }
    if (target == to_) // the same aim again: a slide under way carries on
        return false;
    const auto here = drawn(); // a retarget carries on from where the ghost is drawn
    const bool sameSize = target.getWidth() == to_.getWidth() && target.getHeight() == to_.getHeight();
    const bool smallStep = sameSize && std::abs(target.getX() - to_.getX()) <= kFollowPx &&
                           std::abs(target.getY() - to_.getY()) <= kFollowPx;
    to_ = target;
    if (smallStep && !sliding_) { // the pointer's own steps are followed one to one
        from_ = target.toFloat();
        progress_ = 1.0f;
        return false;
    }
    from_ = here;
    progress_ = 0.0f;
    sliding_ = true;
    return true;
}

void DragGhostSlide::applyTweenAt(float t) noexcept {
    const auto before = drawn();
    progress_ = juce::jlimit(0.0f, 1.0f, t);
    if (progress_ >= 1.0f)
        sliding_ = false;
    if (repaint_)
        repaint_(before.getUnion(drawn()).expanded(8.0f).getSmallestIntegerContainer());
}

void DragGhostSlide::aim(juce::Rectangle<int> target, bool isShowing, juce::VBlankAnimatorUpdater& updater,
                         const std::function<void(juce::Rectangle<int>)>& repaint) {
    repaint_ = repaint;
    const bool animate = !synth::ui::prefersReducedMotion() && !synth::ui::animationsOff() && (isShowing || force_);
    const auto before = drawn();
    if (!retarget(target, animate)) {
        if (!sliding_)
            driver_.stop(updater);
        repaint_(before.getUnion(drawn()).expanded(8.0f).getSmallestIntegerContainer());
        return;
    }
    driver_.start(
        updater, kSlideMs, [](float t) { return t; }, [this](float t) { applyTweenAt(t); },
        [this] { applyTweenAt(1.0f); });
}

void DragGhostSlide::reset(juce::VBlankAnimatorUpdater& updater) {
    driver_.stop(updater);
    to_ = {};
    from_ = {};
    progress_ = 1.0f;
    sliding_ = false;
}

juce::Rectangle<float> DragGhostSlide::drawn() const noexcept {
    const auto to = to_.toFloat();
    if (!sliding_)
        return to;
    const float e = synth::ui::easeOutCubic(progress_);
    return {from_.getX() + (to.getX() - from_.getX()) * e, from_.getY() + (to.getY() - from_.getY()) * e,
            from_.getWidth() + (to.getWidth() - from_.getWidth()) * e,
            from_.getHeight() + (to.getHeight() - from_.getHeight()) * e};
}

juce::Point<float> DragGhostSlide::offset() const noexcept {
    return drawn().getPosition() - to_.toFloat().getPosition();
}
