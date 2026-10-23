// MorphMapGlide.h -- the pure motion model behind holding an arrow on the morph map: when a dot, the cursor or a
// slider starts to glide, how fast it goes as the key stays down, and where it is at any moment. Clock-free (every
// call is handed the time) so it is unit-testable; MorphMap drives it from VBlank frames.
#pragma once

#include "Transport/CursorGlide.h"
#include "UI/Layout/KeyGlide/KeyGlide.h"

namespace synth::ui {

namespace morph_glide {
/** The timeline cursor's tap threshold and ramp length, so both feel the same under the finger. */
inline constexpr double kTapMs = synth::cursor_glide::kTapMs;
inline constexpr double kRampMs = synth::cursor_glide::kRampMs;
/** Map units (the map is 0..1 across) per second when the glide starts, and at the cap reached kRampMs later. */
inline constexpr double kStartUnitsPerSecond = 0.15;
inline constexpr double kMaxUnitsPerSecond = 1.2;
/** Holding Shift multiplies the speed. */
inline constexpr double kShiftSpeedFactor = 3.0;
} // namespace morph_glide

struct GlidePoint {
    double x = 0.0;
    double y = 0.0;
};

class MorphMapGlide {
public:
    /** Starts a hold at `start`. Returns false and changes nothing while a hold is in progress. */
    bool press(double nowMs, GlidePoint start) noexcept;

    bool isHeld() const noexcept { return glide_.isHeld(); }
    /** True once the key has been down past the tap threshold, i.e. the position is moving. */
    bool isGliding(double nowMs) const noexcept;
    /** Units per second (>= 0, before the Shift factor): 0 before the glide starts, then a cubic ease-in to the cap. */
    double velocityAt(double nowMs) const noexcept;

    /** Moves the held position along (dx, dy) (each -1, 0 or 1; both set moves diagonally at the same speed) by what
     *  the glide has covered since the previous call, clamped to 0..1, and returns it. */
    GlidePoint advance(double nowMs, int dx, int dy, double speedFactor = 1.0) noexcept;
    GlidePoint getPosition() const noexcept { return position_; }

    /** Ends the hold and returns where it got to. */
    GlidePoint release() noexcept;

private:
    KeyGlide glide_{KeyGlideConfig{morph_glide::kStartUnitsPerSecond, morph_glide::kMaxUnitsPerSecond,
                                   morph_glide::kTapMs, morph_glide::kRampMs}};
    GlidePoint position_;
};

} // namespace synth::ui
