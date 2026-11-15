#pragma once

/** Rising-arm / falling-rearm detector with a fixed hysteresis gap.
 *
 *  Arms when the sample rises above `threshold`, and only re-arms once it falls
 *  `kHysteresis` below that threshold. Without the gap, a signal loitering near
 *  the threshold — a slow sine, anything with dither on it — would chatter every
 *  sample.
 *
 *  The hysteresis is deliberately not user-exposed (see the Module Development
 *  Guide on not crowding modules with extra knobs). Shared by Sample & Hold,
 *  ADSR, and Comparator so a threshold tweak in one place cannot drift.
 */
struct SchmittTrigger {
    static constexpr float kHysteresis = 0.05f;

    bool high = false;

    enum class Edge { None, Rising, Falling };

    Edge process(float sample, float threshold) noexcept {
        if (!high && sample > threshold) {
            high = true;
            return Edge::Rising;
        }
        if (high && sample < rearmLevel(threshold)) {
            high = false;
            return Edge::Falling;
        }
        return Edge::None;
    }

    void reset() noexcept { high = false; }

    /** The level a high trigger must fall below. A positive threshold keeps the gap only where the level stays
     *  above zero: below twice the gap it re-arms at half the threshold, or a gate resting at 0 would never fall. */
    static constexpr float rearmLevel(float threshold) noexcept {
        return threshold > 0.0f && threshold < 2.0f * kHysteresis ? 0.5f * threshold : threshold - kHysteresis;
    }
};
