#pragma once

// ScrollTween.h -- eases a mouse-wheel NOTCH (a big one-frame jump) over ~120 ms. Trackpad and
// inertial events never come here. Two axes so a diagonal notch shares one driver.

#include "UI/Layout/UIAnimation.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <optional>

namespace synth::ui {

// Pure logic: per axis {from, to, current} in the caller's own units, all relative to the tween's
// start. retarget() ADDS to the remaining distance and rebases from where the view is NOW, so a
// notch mid-tween accumulates rather than restarting or dropping distance.
class ScrollTween {
public:
    static constexpr int kAxes = 2;
    bool isActive() const noexcept { return active_; }
    void stop() noexcept { active_ = false; }

    void retarget(int axis, double amount) noexcept {
        if (!active_) {
            axes_ = {};
            active_ = true;
        }
        for (auto& a : axes_)
            a.from = a.current;
        axes_[(size_t)axis].to += amount;
    }

    // `t` is the driver's eased 0..1 progress through the current segment (>= 1 finishes exactly).
    // apply(axis, delta) returns the delta the view ACTUALLY moved; less than asked means a clamp,
    // which ends that axis. Returns whether the tween is still running.
    template <typename Apply>
    bool step(double t, Apply&& apply) {
        if (!active_)
            return false;
        bool remaining = false;
        for (int i = 0; i < kAxes; ++i) {
            auto& a = axes_[(size_t)i];
            const double target = t >= 1.0 ? a.to : a.from + (a.to - a.from) * t;
            const double want = target - a.current;
            if (want != 0.0) {
                const double got = apply(i, want);
                if (std::abs(got - want) > 1.0e-9 * std::max(1.0, std::abs(want)))
                    a.to = a.from = a.current += got; // clamped: nothing further to travel
                else
                    a.current = target;
            }
            remaining = remaining || a.to != a.current;
        }
        active_ = remaining && t < 1.0;
        return active_;
    }

private:
    struct Axis {
        double from = 0.0, to = 0.0, current = 0.0;
    };
    std::array<Axis, kAxes> axes_{};
    bool active_ = false;
};

// Drives a ScrollTween from a VBlank animator. read(axis) is the view's absolute position and
// scroll(axis, delta) moves it through the owner's existing clamped call; a read that differs from
// where the last step left it means something ELSE moved the view (drag auto-scroll, follow,
// programmatic change), which ends the tween instead of fighting it.
class ScrollTweenRunner {
public:
    static constexpr double kDurationMs = 120.0;
    using Read = std::function<double(int)>;
    using Scroll = std::function<void(int, double)>;

    ~ScrollTweenRunner() { stop(); }

    // False means "not tweened, apply the scroll yourself" (host not showing: headless/tests).
    bool push(juce::Component& host, int axis, double amount, Read read, Scroll scroll) {
        if (!host.isShowing()) {
            stop();
            return false;
        }
        if (tween_.isActive() && movedExternally(read))
            stop();
        if (!tween_.isActive())
            for (int i = 0; i < ScrollTween::kAxes; ++i)
                last_[(size_t)i] = read(i);
        tween_.retarget(axis, amount);
        if (!updater_.has_value())
            updater_.emplace(&host);
        auto frame = [this, read, scroll](float t) { step(t, read, scroll); };
        anim_.start(*updater_, kDurationMs, easeOutCubic, frame, [frame] { frame(1.0f); });
        return true;
    }

    // Idempotent; call before any direct scroll, zoom, clip change or drag start.
    void stop() {
        tween_.stop();
        if (updater_.has_value())
            anim_.stop(*updater_);
    }

    bool isActive() const noexcept { return tween_.isActive(); }

private:
    bool movedExternally(const Read& read) const {
        for (int i = 0; i < ScrollTween::kAxes; ++i)
            if (read(i) != last_[(size_t)i])
                return true;
        return false;
    }

    void step(float t, const Read& read, const Scroll& scroll) {
        bool interrupted = false;
        tween_.step((double)t, [&](int axis, double want) {
            const double before = read(axis);
            if (before != last_[(size_t)axis]) {
                interrupted = true;
                return 0.0;
            }
            scroll(axis, want);
            last_[(size_t)axis] = read(axis);
            return last_[(size_t)axis] - before;
        });
        if (interrupted)
            tween_.stop(); // the animator itself just runs out its last few frames as no-ops
    }

    ScrollTween tween_;
    std::array<double, ScrollTween::kAxes> last_{};
    synth::ui::AnimationDriver anim_;
    std::optional<juce::VBlankAnimatorUpdater> updater_;
};

} // namespace synth::ui
