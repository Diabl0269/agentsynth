#pragma once

#include "UI/Layout/UIAnimation.h"
#include <algorithm>

namespace synth::ui {

// When the user jumps the transport on purpose (a marker, a loop locator, Return to Start), the line the eye follows
// glides from where it was to where the sound now is instead of teleporting. Only the DRAWN cursor glides; the audio
// jumps at once. Pure and clock-injected: the caller passes `nowMs`.
//
// A glide is armed by the code that asked for the jump (arm) and used up by the next locate the overlay sees; a locate
// nobody armed (the ruler scrub, the cursor glide keys, which locate every frame) never glides. An arm that no locate
// followed within kArmWindowMs lapses, so a stale one cannot turn a later scrub into a glide.
class PlayheadLocateGlide {
public:
    static constexpr double kDurationMs = 160.0;
    static constexpr double kArmWindowMs = 1500.0;

    void arm(double nowMs) noexcept {
        armed_ = true;
        armedAtMs_ = nowMs;
    }

    // True once when armed and not lapsed; clears the arm either way.
    bool consumeArm(double nowMs) noexcept {
        const bool valid = armed_ && nowMs - armedAtMs_ <= kArmWindowMs;
        armed_ = false;
        return valid;
    }

    void start(double fromBeat, double nowMs) noexcept {
        running_ = true;
        fromBeat_ = fromBeat;
        startedAtMs_ = nowMs;
    }
    void cancel() noexcept { running_ = false; }

    bool isActive(double nowMs) const noexcept { return running_ && nowMs - startedAtMs_ < kDurationMs; }

    // The drawn beat: `baseBeat` (where the line would be without a glide) while idle and from t = 1, otherwise
    // `from + (base - from) * easeOutCubic(t)`. The base is read every call, so the glide lands on the live position
    // even while the sound moves on.
    double apply(double baseBeat, double nowMs) const noexcept {
        if (!isActive(nowMs))
            return baseBeat;
        const double t = std::clamp((nowMs - startedAtMs_) / kDurationMs, 0.0, 1.0);
        return fromBeat_ + (baseBeat - fromBeat_) * (double)easeOutCubic((float)t);
    }

private:
    bool armed_ = false;
    double armedAtMs_ = 0.0;
    bool running_ = false;
    double fromBeat_ = 0.0;
    double startedAtMs_ = 0.0;
};

} // namespace synth::ui
