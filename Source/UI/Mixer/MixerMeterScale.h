#pragma once

#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

// MixerMeterScale.h -- FRO146 (docs/mixer.md meters section): the meter's dB scale, shared by the
// painter (MixerMeter), the clip readout (MixerMeterReadout) and the track header's channel chip
// (ChannelChipComponent), and unit-tested directly so the boundary behaviour never has to be
// reverse-engineered from pixels.
//
// Cubase's own default channel-meter scale ("+3 dB Digital"): -60..+3 dB, mapped LINEARLY IN DB
// (not linear-in-amplitude) to a 0..1 fraction of the bar's length -- this is what keeps the low
// end of the scale legible. Below -60 dB reads empty; above +3 dB clamps to full.

namespace synth::ui {

inline constexpr float kMeterMinDb = -60.0f;
inline constexpr float kMeterMaxDb = 3.0f;

// Tick marks the meter draws, loudest first -- the 0 dB entry is drawn visibly stronger than the
// rest (MixerMeter::paint's own job, not this table's).
inline constexpr std::array<float, 9> kMeterTickDb{3.0f, 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f};

/** Converts a linear amplitude peak (0..1-ish; a real over can exceed 1) to dBFS, floored at
 *  kMeterMinDb. NaN-safe: `linearPeak > 0.0f` is false for NaN (and for silence/negative input),
 *  so both fall through to the floor rather than propagating a NaN into the ballistics. */
inline float meterLinearToDb(float linearPeak) noexcept {
    if (!(linearPeak > 0.0f))
        return kMeterMinDb;
    return juce::jmax(kMeterMinDb, juce::Decibels::gainToDecibels(linearPeak));
}

/** Maps a dB value to the 0 (bottom, kMeterMinDb or below) .. 1 (top, kMeterMaxDb or above)
 *  fraction of the bar's length -- linear in dB, per the class comment above. */
inline float meterDbToFraction(float db) noexcept {
    return juce::jlimit(0.0f, 1.0f, (db - kMeterMinDb) / (kMeterMaxDb - kMeterMinDb));
}

} // namespace synth::ui
