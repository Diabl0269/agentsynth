#pragma once

#include "UI/Layout/UIAnimation.h"
#include <functional>

// ReorderFramePump.h: the VBlank glue between a ReorderDragAnimator and its owning component.
// The animator computes positions from its clock; this runs the owner's frame callback on every
// VBlank while a tween is in flight and stops on its own, so a settled list costs no repaints.
namespace synth::ui {

class ReorderFramePump {
public:
    /** `owner` must outlive the pump; no frame arrives while it is not showing. */
    explicit ReorderFramePump(juce::Component& owner)
        : updater_(&owner) {}

    /** Runs `onFrame` each VBlank for `durationMs`, then once more and `onDone`. Replaces a run in
     *  flight. Never call from inside `onFrame` or `onDone`. */
    void run(double durationMs, std::function<void()> onFrame, std::function<void()> onDone = {}) {
        auto frame = [onFrame](float) { onFrame(); };
        auto done = [onFrame, onDone = std::move(onDone)] {
            onFrame();
            if (onDone)
                onDone();
        };
        driver_.start(updater_, durationMs, [](float t) { return t; }, std::move(frame), std::move(done));
    }

    void stop() { driver_.stop(updater_); }

private:
    juce::VBlankAnimatorUpdater updater_;
    AnimationDriver driver_;
};

} // namespace synth::ui
