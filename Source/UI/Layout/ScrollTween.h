#pragma once

// ScrollTween.h -- eases a mouse-wheel NOTCH (a big one-frame jump) over ~120 ms. A trackpad's vertical events go to
// ScrollFrameSmoother at the end of this file instead. Two axes so a diagonal notch shares one driver.

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
    // A view stored in floats moves by a rounded delta (a pan of -400.3 asked as 12.7 lands 12.69998): that is not a
    // clamp. Less than this much short of the ask is the float rounding; more is a clamp that ends the axis.
    static constexpr double kClampSlackPx = 1.0e-2;
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
                if (std::abs(got - want) > kClampSlackPx)
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

    // Flies the view by (dx, dy) from where it is now over `durationMs` (easeOutCubic), replacing any tween running.
    // The same external-move rule ends it: a pan, drag or zoom that moves the view mid-flight wins. False means "not
    // flown, apply it yourself" (host not showing and no test opt-in).
    bool flyBy(juce::Component& host, double dx, double dy, double durationMs, Read read, Scroll scroll) {
        if (!host.isShowing() && !animateOffScreenForTest_) {
            stop();
            return false;
        }
        stop();
        for (int i = 0; i < ScrollTween::kAxes; ++i)
            last_[(size_t)i] = read(i);
        tween_.retarget(0, dx);
        tween_.retarget(1, dy);
        if (!updater_.has_value())
            updater_.emplace(&host);
        auto frame = [this, read, scroll](float t) { step(t, read, scroll); };
        anim_.start(*updater_, durationMs, easeOutCubic, frame, [frame] { frame(1.0f); });
        return true;
    }

    /** Test seams: let flyBy run off screen, and step it by hand (`ms` after its first frame). */
    void setAnimateOffScreenForTest(bool on) noexcept { animateOffScreenForTest_ = on; }
    void stepForTest(double ms) { anim_.stepForTest(ms); }

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
    bool animateOffScreenForTest_ = false;
    std::array<double, ScrollTween::kAxes> last_{};
    synth::ui::AnimationDriver anim_;
    std::optional<juce::VBlankAnimatorUpdater> updater_;
};

// Paces a TRACKPAD vertical scroll at the display rate. A trackpad sends wheel events at its own rate (about 60 to 125
// a second, unevenly), not the screen's: applying each one at once moves the view several times in one frame and not at
// all in the next. Here the deltas only pile up in a backlog; once per VBlank frame a fraction of the backlog is
// applied (one view move, one repaint), so the step per frame stays even while the finger or the fling moves at a
// steady speed.
class ScrollFrameSmoother {
public:
    // How fast the backlog drains: it loses 1 - e^(-dt / kTauMs) of itself a frame (about 34 percent at 60 Hz, 19 at
    // 120 Hz), so a lone event lands in about 4 frames and a steady stream adds no lag beyond that.
    static constexpr double kTauMs = 40.0;
    // A backlog below this is applied in full: the tail ends instead of creeping for ever.
    static constexpr double kRestPx = 0.5;
    static constexpr double kRunMs = 400.0;      // one bounded run; a backlog left at its end starts the next
    using Apply = std::function<double(double)>; // moves the view by the delta, returns how far it really moved

    ~ScrollFrameSmoother() { cancel(); }

    // False means "not paced, apply it yourself" (host not showing: headless tests, unless the test opts in).
    bool push(juce::Component& host, double deltaPx, Apply apply) {
        if (!host.isShowing() && !offScreenForTest_) {
            cancel();
            return false;
        }
        apply_ = std::move(apply);
        // A reversal drops what is left of the old direction, so the view turns at once instead of coasting on.
        if (backlogPx_ != 0.0 && (deltaPx < 0.0) != (backlogPx_ < 0.0))
            backlogPx_ = 0.0;
        backlogPx_ += deltaPx;
        if (offScreenForTest_ || anim_.isRunning())
            return true;
        lastFrameMs_ = 0.0;
        startRun(host);
        return true;
    }

    // Applies the whole backlog now (a zoom, a notch, a programmatic move must see the view where the finger left it).
    void flush() {
        if (backlogPx_ != 0.0)
            applyStep(backlogPx_);
        cancel();
    }

    // Drops the backlog and stops the run, applying nothing.
    void cancel() {
        backlogPx_ = 0.0;
        anim_.stop();
    }

    bool hasBacklog() const noexcept { return backlogPx_ != 0.0; }
    double backlogPx() const noexcept { return backlogPx_; }

    /** Test seams: no VBlank reaches an off-screen host, so a test runs the frames by hand with the gap it wants. */
    void setOffScreenForTest(bool on) noexcept { offScreenForTest_ = on; }
    void frameForTest(double dtMs) { frame(dtMs); }

    /** What the owner did, for the tests: frames that moved the view, and its syncs (one per frame is the aim). */
    int movedFrames = 0;
    int syncCalls = 0;
    double repaintedPx = 0.0; // area the syncs asked to repaint, summed

private:
    void startRun(juce::Component& host) {
        if (!updater_.has_value())
            updater_.emplace(&host);
        anim_.start(
            *updater_, kRunMs, [](float t) { return t; }, [this](float) { onVBlank(); },
            [this, &host] {
                onVBlank();
                if (backlogPx_ != 0.0)
                    startRun(host);
            });
    }

    void onVBlank() {
        const double now = juce::Time::getMillisecondCounterHiRes();
        const double dt = lastFrameMs_ > 0.0 ? now - lastFrameMs_ : 1000.0 / 60.0;
        lastFrameMs_ = now;
        frame(dt);
    }

    void frame(double dtMs) {
        if (backlogPx_ == 0.0)
            return;
        const double keep = std::exp(-std::clamp(dtMs, 1.0, 50.0) / kTauMs);
        double step = backlogPx_ * (1.0 - keep);
        if (std::abs(backlogPx_) < kRestPx || std::abs(backlogPx_ - step) < kRestPx)
            step = backlogPx_;
        applyStep(step);
    }

    void applyStep(double step) {
        const double want = step;
        backlogPx_ -= step;
        if (std::abs(backlogPx_) < 1.0e-9)
            backlogPx_ = 0.0;
        const double got = apply_ ? apply_(want) : want;
        ++movedFrames;
        if (std::abs(got - want) > 1.0e-2) // the view hit an end: nothing further to travel, so no creep back
            backlogPx_ = 0.0;
    }

    double backlogPx_ = 0.0;
    double lastFrameMs_ = 0.0;
    bool offScreenForTest_ = false;
    Apply apply_;
    synth::ui::AnimationDriver anim_;
    std::optional<juce::VBlankAnimatorUpdater> updater_;
};

// WheelScrollRouter -- the one place a scroll surface decides how a wheel event reaches its view. A mouse notch eases
// through ScrollTweenRunner; a trackpad's deltas (isSmooth or isInertial) pile up per axis in a ScrollFrameSmoother and
// land once per VBlank frame, so the step per frame stays even at 60 and 120 Hz. The surface keeps what is its own: the
// gesture's axis and amount (in pixels), the zoom branch, and how its view moves and repaints.
class WheelScrollRouter {
public:
    static constexpr int kAxes = ScrollTween::kAxes;
    using Read = ScrollTweenRunner::Read;
    using Scroll = ScrollTweenRunner::Scroll;

    /** True when the amount was taken: eased in (a notch) or queued for the next frame (a trackpad). False means "not
     *  paced, apply it yourself now" (host not showing: headless tests); any tween or backlog is already ended, so the
     *  direct move cannot be fought. `read(axis)` is the view's position, `scroll(axis, delta)` moves it by a clamped
     *  delta; the distance a frame really moved is read back from `read`, so a view that hit its end drops the rest. */
    bool route(juce::Component& host, int axis, double amount, bool eased, Read read, Scroll scroll) {
        if (eased) {
            flush(); // a notch after a swipe starts from where the swipe's backlog ends
            if (tween_.push(host, axis, amount, read, scroll))
                return true;
        } else {
            tween_.stop();
            auto step = [axis, read = std::move(read), scroll = std::move(scroll)](double d) {
                const double before = read(axis);
                scroll(axis, d);
                return read(axis) - before;
            };
            if (pads_[(size_t)axis].push(host, amount, std::move(step)))
                return true;
        }
        stop();
        return false;
    }

    /** Lands every backlog now and ends the tween: a zoom or a jump must see the view where the finger left it. */
    void settle() {
        flush();
        tween_.stop();
    }

    /** Ends everything, applying nothing: the view is about to be moved or replaced by someone else. */
    void stop() {
        tween_.stop();
        for (auto& p : pads_)
            p.cancel();
    }

    /** Test seams. */
    ScrollFrameSmoother& pad(int axis) noexcept { return pads_[(size_t)axis]; }
    ScrollTweenRunner& tween() noexcept { return tween_; }
    bool isActive() const noexcept { return tween_.isActive(); }
    void setAnimateOffScreenForTest(bool on) noexcept { tween_.setAnimateOffScreenForTest(on); }
    void stepForTest(double ms) { tween_.stepForTest(ms); }
    void setOffScreenForTest(bool on) noexcept {
        for (auto& p : pads_)
            p.setOffScreenForTest(on);
    }
    void frameForTest(double dtMs) {
        for (auto& p : pads_)
            p.frameForTest(dtMs);
    }

private:
    void flush() {
        for (auto& p : pads_)
            p.flush();
    }

    ScrollTweenRunner tween_;
    std::array<ScrollFrameSmoother, kAxes> pads_;
};

} // namespace synth::ui
