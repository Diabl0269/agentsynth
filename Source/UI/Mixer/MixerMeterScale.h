#pragma once

#include <array>
#include <cstddef>
#include <juce_audio_basics/juce_audio_basics.h>

// MixerMeterScale.h -- FRO146 (docs/mixer.md meters section): the meter's dB scale, shared by the
// painter (MixerMeter), the clip readout (MixerMeterReadout) and the track header's channel chip
// (ChannelChipComponent), and unit-tested directly so the boundary behaviour never has to be
// reverse-engineered from pixels.
//
// -60..+3 dB (our own top; Cubase's own default channel-meter scale has no +3 mark, but we keep
// ours as the pre-existing clip headroom cap). The dB<->position mapping is NOT linear in dB --
// FRO146 follow-up: Cubase's real channel-meter taper gives 0 dB (and the +3 headroom above it)
// generous room and compresses the -30..-60 tail into a small strip at the bottom, so a meter reads
// legibly at typical mixing levels without the quiet end turning into a single illegible pixel.
// meterDbToFraction()/meterFractionToDb() are monotonic PIECEWISE-LINEAR interpolations through the
// breakpoints below (== kMeterTickDb's own values) -- the one mapping every caller (ticks, the bar
// fill via MeterColourStops::forEachBand's band edges, the ballistics-independent peak-hold line,
// and ChannelChipComponent's horizontal fill) goes through, so a taper change here is a single-file
// change. A later ticket gives the FADER its own Cubase-style taper -- deliberately a SEPARATE
// mapping (faders and meters read differently in Cubase itself), so nothing here is reused there.

namespace synth::ui {

inline constexpr float kMeterMinDb = -60.0f;
inline constexpr float kMeterMaxDb = 3.0f;

// Tick marks the meter draws, loudest first -- the 0 dB entry is drawn visibly stronger than the
// rest (MixerMeter::paint's own job, not this table's). Matches Cubase's own channel-meter marks
// (0, -6, -12, -18, -24, -30, -40, -50, -60) plus our own +3 dB headroom cap at the top.
inline constexpr std::array<float, 10> kMeterTickDb{3.0f,   0.0f,   -6.0f,  -12.0f, -18.0f,
                                                    -24.0f, -30.0f, -40.0f, -50.0f, -60.0f};

namespace detail {

struct MeterTaperBreakpoint {
    float db;
    float fraction; // 0 (kMeterMinDb) .. 1 (kMeterMaxDb)
};

// The taper itself, ascending by db -- the SAME ten values as kMeterTickDb (reversed), each paired
// with its own position. Positions were measured off a Cubase MixConsole channel meter: every
// 6 dB from 0 to -24 gets ~12.5% of the bar, -24..-30 ~11%, -30..-40 ~14.5%, -40..-50 ~9.5%,
// -50..-60 ~7%, and our own +3 dB headroom above 0 the top 8%. meterDbToFraction()/meterFractionToDb() below
// linearly interpolate WITHIN each segment; every segment has strictly positive db and fraction
// spans, so the mapping (and its inverse) are strictly monotonic end to end.
inline constexpr std::array<MeterTaperBreakpoint, 10> kMeterTaperBreakpoints{{
    {-60.0f, 0.00f},
    {-50.0f, 0.07f},
    {-40.0f, 0.165f},
    {-30.0f, 0.31f},
    {-24.0f, 0.42f},
    {-18.0f, 0.545f},
    {-12.0f, 0.67f},
    {-6.0f, 0.795f},
    {0.0f, 0.92f},
    {3.0f, 1.00f},
}};

} // namespace detail

/** Converts a linear amplitude peak (0..1-ish; a real over can exceed 1) to dBFS, floored at
 *  kMeterMinDb. NaN-safe: `linearPeak > 0.0f` is false for NaN (and for silence/negative input),
 *  so both fall through to the floor rather than propagating a NaN into the ballistics. */
inline float meterLinearToDb(float linearPeak) noexcept {
    if (!(linearPeak > 0.0f))
        return kMeterMinDb;
    return juce::jmax(kMeterMinDb, juce::Decibels::gainToDecibels(linearPeak));
}

/** Maps a dB value to the 0 (bottom, kMeterMinDb or below) .. 1 (top, kMeterMaxDb or above)
 *  fraction of the bar's length -- Cubase's own piecewise-linear taper through
 *  detail::kMeterTaperBreakpoints, per the class comment above (NOT linear in dB). */
inline float meterDbToFraction(float db) noexcept {
    const float clamped = juce::jlimit(kMeterMinDb, kMeterMaxDb, db);
    const auto& bp = detail::kMeterTaperBreakpoints;
    for (std::size_t i = 1; i < bp.size(); ++i) {
        if (clamped <= bp[i].db) {
            const float span = bp[i].db - bp[i - 1].db;
            const float t = span > 0.0f ? (clamped - bp[i - 1].db) / span : 0.0f;
            return bp[i - 1].fraction + t * (bp[i].fraction - bp[i - 1].fraction);
        }
    }
    return 1.0f; // unreachable -- clamped can never exceed bp.back().db (== kMeterMaxDb)
}

/** The inverse of meterDbToFraction() -- a 0..1 bar-length fraction back to its dB value, through
 *  the SAME breakpoint table. Not used by any painter today (every caller already has a dB value in
 *  hand); exists so the mapping is provably invertible (see MixerMeterScaleTests.cpp's round-trip
 *  cases) ahead of a later ticket giving the fader its own separate taper. */
inline float meterFractionToDb(float fraction) noexcept {
    const float clamped = juce::jlimit(0.0f, 1.0f, fraction);
    const auto& bp = detail::kMeterTaperBreakpoints;
    for (std::size_t i = 1; i < bp.size(); ++i) {
        if (clamped <= bp[i].fraction) {
            const float span = bp[i].fraction - bp[i - 1].fraction;
            const float t = span > 0.0f ? (clamped - bp[i - 1].fraction) / span : 0.0f;
            return bp[i - 1].db + t * (bp[i].db - bp[i - 1].db);
        }
    }
    return kMeterMaxDb; // unreachable -- clamped can never exceed bp.back().fraction (== 1.0f)
}

} // namespace synth::ui
