// Concern: the pure motion model behind holding the cursor-glide keys -- when the cursor starts to
// move, how fast it goes as the key stays down, and where it lands on release. Clock-free (every
// call is handed the time) so it is unit-testable with a test clock; the UI collaborator that
// drives it from VBlank frames is TimelineCursorGlide.
#pragma once

#include <algorithm>
#include <cmath>

namespace synth {

enum class GlideDirection { Back = -1, Forward = 1 };

namespace cursor_glide {
/** A key held for less than this is a tap: no motion at all, one step on release. */
inline constexpr double kTapMs = 150.0;
/** Speed when the glide starts, in beats (quarter notes) per second. */
inline constexpr double kStartBeatsPerSecond = 1.0;
/** Speed cap, in bars per second, reached kRampMs after the glide starts. */
inline constexpr double kMaxBarsPerSecond = 8.0;
inline constexpr double kRampMs = 2000.0;
/** The landing settle after a tap, or after a release with snap on (easeOutCubic). */
inline constexpr double kSettleMs = 140.0;
/** Without snap a tap moves one beat; with snap, one grid step. */
inline constexpr double kTapStepBeatsWithoutSnap = 1.0;
} // namespace cursor_glide

class CursorGlide {
public:
    /** Starts a hold at `startBeat`. Returns false and changes nothing while a hold is already in
     *  progress: OS key repeat delivers further key-downs, and none of them may restart the ramp. */
    bool press(GlideDirection direction, double nowMs, double startBeat, double beatsPerBar) noexcept {
        if (held_)
            return false;
        held_ = true;
        direction_ = direction;
        pressedAtMs_ = nowMs;
        startBeat_ = std::max(0.0, startBeat);
        const double vMax = cursor_glide::kMaxBarsPerSecond * (beatsPerBar > 0.0 ? beatsPerBar : 4.0);
        maxVelocity_ = std::max(vMax, cursor_glide::kStartBeatsPerSecond);
        return true;
    }

    bool isHeld() const noexcept { return held_; }
    GlideDirection getDirection() const noexcept { return direction_; }

    /** True once the key has been down past the tap threshold, i.e. the cursor is moving. */
    bool isGliding(double nowMs) const noexcept { return held_ && nowMs - pressedAtMs_ >= cursor_glide::kTapMs; }

    /** Speed in beats per second (always >= 0): 0 before the glide starts, then an ease-in (cubic)
     *  from kStartBeatsPerSecond up to the cap, flat after kRampMs. */
    double velocityAt(double nowMs) const noexcept {
        if (!isGliding(nowMs))
            return 0.0;
        const double u = std::min(1.0, glideMs(nowMs) / cursor_glide::kRampMs);
        return cursor_glide::kStartBeatsPerSecond + (maxVelocity_ - cursor_glide::kStartBeatsPerSecond) * u * u * u;
    }

    /** The cursor position the hold has reached by `nowMs`: the exact integral of velocityAt, so
     *  the motion does not depend on the frame rate. Clamped at beat 0. */
    double positionAt(double nowMs) const noexcept {
        if (!isGliding(nowMs))
            return startBeat_;
        const double t = glideMs(nowMs) / 1000.0;
        const double ramp = cursor_glide::kRampMs / 1000.0;
        const double v0 = cursor_glide::kStartBeatsPerSecond;
        const double rise = maxVelocity_ - v0;
        const double distance = t <= ramp ? v0 * t + rise * ramp * std::pow(t / ramp, 4.0) / 4.0
                                          : v0 * ramp + rise * ramp / 4.0 + maxVelocity_ * (t - ramp);
        return std::max(0.0, startBeat_ + (double)direction_ * distance);
    }

    struct Landing {
        double from = 0.0;   // where the cursor is when the key goes up
        double target = 0.0; // where it ends up
        bool settle = false; // true: ease from `from` to `target`; false: it is already there
    };

    /** Ends the hold. `gridBeats` is the snap step, or 0 when snap is off.
     *  - A tap (held under kTapMs) lands one grid step away (next grid line in the direction
     *    pressed) with snap on, one beat away with it off, and always settles there.
     *  - A hold lands where it got to, and with snap on settles to the nearest grid line. Snap
     *    applies to this final landing only; the motion before it is free. */
    Landing release(double nowMs, double gridBeats) noexcept {
        Landing landing;
        landing.from = positionAt(nowMs);
        if (!isGliding(nowMs)) {
            landing.target = tapTarget(gridBeats);
            landing.settle = landing.target != landing.from;
        } else if (gridBeats > 0.0) {
            landing.target = std::max(0.0, std::floor(landing.from / gridBeats + 0.5) * gridBeats);
            landing.settle = landing.target != landing.from;
        } else {
            landing.target = landing.from;
        }
        held_ = false;
        return landing;
    }

    /** Drops a hold without landing anywhere. */
    void cancel() noexcept { held_ = false; }

private:
    double glideMs(double nowMs) const noexcept { return nowMs - pressedAtMs_ - cursor_glide::kTapMs; }

    double tapTarget(double gridBeats) const noexcept {
        const double dir = (double)direction_;
        if (gridBeats <= 0.0)
            return std::max(0.0, startBeat_ + dir * cursor_glide::kTapStepBeatsWithoutSnap);
        // The next grid line strictly beyond the start, so a cursor already on a line moves a full step.
        constexpr double eps = 1e-9;
        const double lines = startBeat_ / gridBeats;
        const double next =
            direction_ == GlideDirection::Forward ? std::floor(lines + eps) + 1.0 : std::ceil(lines - eps) - 1.0;
        return std::max(0.0, next * gridBeats);
    }

    bool held_ = false;
    GlideDirection direction_ = GlideDirection::Forward;
    double pressedAtMs_ = 0.0;
    double startBeat_ = 0.0;
    double maxVelocity_ = cursor_glide::kStartBeatsPerSecond;
};

} // namespace synth
