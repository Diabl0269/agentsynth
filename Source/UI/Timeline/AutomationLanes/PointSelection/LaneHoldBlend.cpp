#include "LaneHoldBlend.h"
#include "UI/Layout/ReducedMotion.h"

namespace synth::ui {

LaneHoldBlend::LaneHoldBlend(juce::Component& owner)
    : owner_(owner)
    , vblank_(&owner) {}

LaneHoldBlend::~LaneHoldBlend() { driver_.stop(vblank_); }

void LaneHoldBlend::reset(bool hold) {
    driver_.stop(vblank_);
    running_ = false;
    target_ = hold;
    amount_ = hold ? 1.0f : 0.0f;
}

// Reverses from wherever it is, so a drag that crosses the threshold back and forth never jumps.
void LaneHoldBlend::setHold(bool hold) {
    if (hold == target_)
        return;
    target_ = hold;
    const float from = amount_;
    const float to = hold ? 1.0f : 0.0f;
    if (!(forceAnimateForTest_ || owner_.isShowing()) || prefersReducedMotion() || animationsOff()) {
        driver_.stop(vblank_);
        running_ = false;
        amount_ = to;
        owner_.repaint();
        return;
    }
    running_ = true;
    driver_.start(
        vblank_, kMs, easeOutCubic,
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
