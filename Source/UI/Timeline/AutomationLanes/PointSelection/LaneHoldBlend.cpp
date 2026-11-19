#include "LaneHoldBlend.h"
#include "UI/Layout/ReducedMotion.h"
#include <cmath>

namespace synth::ui {

LaneHoldBlend::LaneHoldBlend(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

LaneHoldBlend::~LaneHoldBlend() { driver_.stop(vblank_); }

void LaneHoldBlend::reset(bool hold) {
    driver_.stop(vblank_);
    running_ = false;
    target_ = hold ? 1.0f : 0.0f;
    amount_ = target_;
}

void LaneHoldBlend::setHold(bool hold) {
    const float to = hold ? 1.0f : 0.0f;
    if (to != target_)
        easeTo(to);
}

void LaneHoldBlend::follow(float amount) {
    const float to = juce::jlimit(0.0f, 1.0f, amount);
    if (to == target_)
        return;
    if (running_ || std::abs(to - amount_) > kFollowStep) {
        easeTo(to);
        return;
    }
    target_ = to;
    amount_ = to;
    owner_.repaint();
}

bool LaneHoldBlend::landsAtOnce() const {
    return !(forceAnimateForTest_ || owner_.isShowing()) || prefersReducedMotion() || animationsOff();
}

// Reverses from wherever it is, so a drag that crosses the threshold back and forth never jumps.
void LaneHoldBlend::easeTo(float to) {
    target_ = to;
    const float from = amount_;
    if (landsAtOnce()) {
        driver_.stop(vblank_);
        running_ = false;
        amount_ = to;
        owner_.repaint();
        return;
    }
    running_ = true;
    driver_.start(
        vblank_, kMs, easeInOutCubic,
        [this, from, to](float e) {
            amount_ = from + (to - from) * e;
            owner_.repaint();
        },
        [this, to] {
            running_ = false;
            amount_ = to;
            owner_.repaint();
        });
}

} // namespace synth::ui
