#include "VelocityLaneSlide.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

// Stops a tween still running when the roll goes away: its callbacks reach back into the roll, and
// the updater is attached to it.
VelocityLaneSlide::~VelocityLaneSlide() {
    if (updater_.has_value())
        driver_.stop(*updater_);
}

int VelocityLaneSlide::revealedHeight(float progress, int fullHeight) noexcept {
    const float p = std::clamp(progress, 0.0f, 1.0f);
    return (int)std::lround((double)p * (double)std::max(0, fullHeight));
}

void VelocityLaneSlide::setProgress(float progress) noexcept { progress_ = std::clamp(progress, 0.0f, 1.0f); }

void VelocityLaneSlide::finish() {
    if (updater_.has_value())
        driver_.stop(*updater_);
    progress_ = to_; // pin the EXACT end value
}

// Same shape as the scale panel's slide: the tween starts from wherever the strip is right now, so
// a mid-flight toggle reverses from there instead of jumping to an extreme first. The caller's
// duration and easing (easeInOutCubic) are the scale panel's, so the two panels move alike.
void VelocityLaneSlide::start(juce::Component& owner, float target, bool animate, double durationMs,
                              std::function<void()> onFrame, std::function<void()> onFinish) {
    from_ = progress_;
    to_ = target;
    if (!animate || !owner.isShowing()) {
        // No VBlank reaches an off-screen component, and a persisted restore must never play a
        // slide: land on the target immediately.
        finish();
        if (onFinish)
            onFinish();
        return;
    }
    if (!updater_.has_value())
        updater_.emplace(&owner); // lazily: the updater is tied to the component, which must exist
    const float from = from_;
    const float to = to_;
    driver_.start(
        *updater_, durationMs, easeInOutCubic,
        [this, from, to, onFrame](float t) {
            setProgress(from + (to - from) * t);
            if (onFrame)
                onFrame();
        },
        [this, onFinish] {
            finish();
            if (onFinish)
                onFinish();
        });
}

} // namespace synth::ui
