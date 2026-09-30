#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

// VelocityLaneSlide — the open fraction of the piano roll's velocity strip and the tween that moves
// it. Pure state plus one AnimationDriver: the roll derives its geometry from progress() and
// re-lays out from the callbacks. See VelocityLaneSlide.cpp and docs/timeline/piano-roll.md.
namespace synth::ui {

class VelocityLaneSlide {
public:
    VelocityLaneSlide() = default;
    ~VelocityLaneSlide();
    VelocityLaneSlide(const VelocityLaneSlide&) = delete;
    VelocityLaneSlide& operator=(const VelocityLaneSlide&) = delete;

    // 0 = fully hidden, 1 = fully shown. Starts shown.
    float progress() const noexcept { return progress_; }
    float animFrom() const noexcept { return from_; }
    float animTo() const noexcept { return to_; }
    bool isRunning() const noexcept { return driver_.isRunning(); }

    // The height (px) the grid gives up at `progress` for a strip `fullHeight` tall.
    static int revealedHeight(float progress, int fullHeight) noexcept;

    // Sets the fraction directly (clamped); a test seam and the per-frame write.
    void setProgress(float progress) noexcept;

    // Retargets from the CURRENT progress to `target` (0 or 1). `animate` false, or `owner` not on
    // screen, lands at once; either way `onFinish` runs exactly once, at the end. `onFrame` runs
    // after every intermediate progress write.
    void start(juce::Component& owner, float target, bool animate, double durationMs, std::function<void()> onFrame,
               std::function<void()> onFinish);

    // Stops any running tween and pins progress to the target.
    void finish();

private:
    float progress_ = 1.0f;
    float from_ = 1.0f;
    float to_ = 1.0f;
    AnimationDriver driver_;
    std::optional<juce::VBlankAnimatorUpdater> updater_;
};

} // namespace synth::ui
