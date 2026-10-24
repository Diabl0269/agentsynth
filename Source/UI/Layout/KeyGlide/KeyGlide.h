// KeyGlide.h -- the pure motion model behind holding a key to move something: when the glide starts, how fast it goes
// as the key stays down, and how far it has covered. Clock-free (every call is handed the time) so it is
// unit-testable; the owner drives it from VBlank frames and applies direction and limits itself.
#pragma once

namespace synth::ui {

struct KeyGlideConfig {
    /** Units per second when the glide starts (after the tap threshold), and at the cap reached rampMs later. */
    double startUnitsPerSecond = 1.0;
    double maxUnitsPerSecond = 8.0;
    /** A key held for less than this is a tap: no motion from the glide. */
    double tapMs = 150.0;
    double rampMs = 2000.0;
};

class KeyGlide {
public:
    explicit KeyGlide(KeyGlideConfig config = {}) noexcept
        : config_(config) {}

    const KeyGlideConfig& config() const noexcept { return config_; }

    /** Starts a hold. Returns false and changes nothing while a hold is in progress. */
    bool press(double nowMs) noexcept;

    bool isHeld() const noexcept { return held_; }
    /** True once the key has been down past the tap threshold, i.e. the position is moving. */
    bool isGliding(double nowMs) const noexcept;
    /** Units per second (>= 0, before any speed factor): 0 before the glide starts, then a cubic ease-in to the cap. */
    double velocityAt(double nowMs) const noexcept;

    /** The distance (>= 0) covered since the previous call (or the press), times `speedFactor`; 0 when not held. */
    double advance(double nowMs, double speedFactor = 1.0) noexcept;

    void release() noexcept { held_ = false; }

private:
    double distanceAt(double nowMs) const noexcept;

    KeyGlideConfig config_;
    bool held_ = false;
    double pressedAtMs_ = 0.0;
    double lastDistance_ = 0.0;
};

} // namespace synth::ui
