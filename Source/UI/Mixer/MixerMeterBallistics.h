#pragma once

#include "MixerMeterScale.h"
#include <algorithm>

// MixerMeterBallistics.h -- FRO146 (docs/mixer.md meters section): one bar's ballistics state
// (instant attack, ~20 dB/s release) plus its peak-hold line (holds 1.5 s once nothing louder
// arrives, then falls at the same ~20 dB/s). Pure data + one advance() function, driven by an
// explicit elapsed time so it is rate-independent of the 10 Hz poll's actual cadence (which is
// gated on the mixer tab being visible, so ticks are not always exactly 100 ms apart) and directly
// testable with no juce::Timer and no wall-clock read anywhere in this file.

namespace synth::ui {

inline constexpr float kMeterReleaseDbPerSecond = 20.0f;
inline constexpr float kMeterPeakHoldSeconds = 1.5f;
inline constexpr float kMeterPeakHoldFallDbPerSecond = 20.0f;

struct MeterBallisticsState {
    float displayedDb = kMeterMinDb; // the bar's own drawn level
    float peakHoldDb = kMeterMinDb;  // the thin peak-hold line's drawn level
    float peakHoldRemainingSeconds = 0.0f;
};

/** Advances one bar's ballistics by `elapsedSeconds` toward `inputDb` (already dBFS -- see
 *  meterLinearToDb). Attack is instant: a louder input snaps the bar straight up. Release falls at
 *  kMeterReleaseDbPerSecond once the input is quieter than the bar. The peak-hold line snaps to a
 *  new peak instantly too, holds there for kMeterPeakHoldSeconds once nothing louder arrives, then
 *  falls at kMeterPeakHoldFallDbPerSecond -- the same release shape as the bar, just delayed. A
 *  tick whose elapsed time spans the hold-to-fall transition splits it: the portion still inside
 *  the hold window is spent holding, and the REMAINDER falls in this same call -- so a poll that
 *  happens to straddle the 1.5 s mark doesn't lose a partial tick's worth of fall. */
inline void advanceMeterBallistics(MeterBallisticsState& state, float inputDb, float elapsedSeconds) noexcept {
    const float release = kMeterReleaseDbPerSecond * elapsedSeconds;
    state.displayedDb = inputDb > state.displayedDb ? inputDb : std::max(inputDb, state.displayedDb - release);

    if (inputDb >= state.peakHoldDb) {
        state.peakHoldDb = inputDb;
        state.peakHoldRemainingSeconds = kMeterPeakHoldSeconds;
        return;
    }

    float remainingElapsed = elapsedSeconds;
    if (state.peakHoldRemainingSeconds > 0.0f) {
        const float holdConsumed = std::min(state.peakHoldRemainingSeconds, remainingElapsed);
        state.peakHoldRemainingSeconds -= holdConsumed;
        remainingElapsed -= holdConsumed;
    }
    if (remainingElapsed > 0.0f) {
        const float fall = kMeterPeakHoldFallDbPerSecond * remainingElapsed;
        state.peakHoldDb = std::max(inputDb, state.peakHoldDb - fall);
    }
}

} // namespace synth::ui
