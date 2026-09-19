#pragma once

#include <array>
#include <cstddef>
#include <juce_core/juce_core.h>

// MixerFaderTaper.h -- FRO150 (docs/mixer/fader.md): the FADER's own Cubase-like dB<->position
// taper. Deliberately a SEPARATE mapping from the METER's own taper (MixerMeterScale.h) -- Cubase's
// fader and meter read differently, and MixerMeterScale.h's own class comment already promised this
// file would never be reused there.
//
// UI-only. The PARAMETER stays linear dB (ChannelStripModule::kMinGainDb/kMaxGainDb,
// MasterModule's identical pair -- -60..+12) so presets, automation, AI patches and the plugin host
// keep reading/writing that linear value unchanged. Only the slider's on-screen THUMB POSITION
// follows this taper -- MixerFader::bind() wires faderDbToFraction/faderFractionToDb in as a
// juce::NormalisableRange<double>'s convertTo0to1/convertFrom0to1, which is exactly the JUCE seam
// meant for this (it maps the slider's VALUE to the 0..1 proportion used for the thumb's position
// and for interpreting drag distance, never touching the value itself).

namespace synth::ui {

inline constexpr float kFaderMinDb = -60.0f;
inline constexpr float kFaderMaxDb = 12.0f;

namespace detail {

struct FaderTaperBreakpoint {
    float db;
    float fraction; // 0 (kFaderMinDb) .. 1 (kFaderMaxDb)
};

// Ascending by db. Measured off Cubase's own MixConsole fader (0 dB sits at 71% of travel) and
// extended up to our own +12 dB top (MixerFader.h's own comment: ChannelStripModule/MasterModule
// share one gain range). Every segment has a strictly positive db AND fraction span, so the
// mapping (and its inverse below) are strictly monotonic end to end -- see
// MixerFaderTaperTests.cpp's round-trip cases.
inline constexpr std::array<FaderTaperBreakpoint, 11> kFaderTaperBreakpoints{{
    {-60.0f, 0.000f},
    {-50.0f, 0.040f},
    {-40.0f, 0.073f},
    {-30.0f, 0.130f},
    {-20.0f, 0.230f},
    {-15.0f, 0.308f},
    {-10.0f, 0.409f},
    {-5.0f, 0.537f},
    {0.0f, 0.710f},
    {6.0f, 0.900f},
    {12.0f, 1.000f},
}};

} // namespace detail

/** dB -> the fader's 0 (bottom, kFaderMinDb or below) .. 1 (top, kFaderMaxDb or above) thumb
 *  position -- Cubase's own piecewise-linear taper through detail::kFaderTaperBreakpoints. The
 *  final jlimit is deliberate, not defensive-for-show: juce::NormalisableRange::convertTo0to1
 *  re-clamps whatever this returns and ASSERTS (Debug) that the clamp was a no-op, so the result
 *  must already be exactly within [0,1], not just "close" -- float interpolation landing a hair
 *  outside at an exact breakpoint (e.g. the top one) would otherwise trip that assertion. */
inline float faderDbToFraction(float db) noexcept {
    const float clamped = juce::jlimit(kFaderMinDb, kFaderMaxDb, db);
    const auto& bp = detail::kFaderTaperBreakpoints;
    for (std::size_t i = 1; i < bp.size(); ++i) {
        if (clamped <= bp[i].db) {
            const float span = bp[i].db - bp[i - 1].db;
            const float t = span > 0.0f ? (clamped - bp[i - 1].db) / span : 0.0f;
            const float fraction = bp[i - 1].fraction + t * (bp[i].fraction - bp[i - 1].fraction);
            return juce::jlimit(0.0f, 1.0f, fraction);
        }
    }
    return 1.0f; // unreachable -- clamped can never exceed bp.back().db (== kFaderMaxDb)
}

/** The inverse of faderDbToFraction() -- a 0..1 thumb-position fraction back to its dB value,
 *  through the SAME breakpoint table. Same final-clamp reasoning as above. */
inline float faderFractionToDb(float fraction) noexcept {
    const float clamped = juce::jlimit(0.0f, 1.0f, fraction);
    const auto& bp = detail::kFaderTaperBreakpoints;
    for (std::size_t i = 1; i < bp.size(); ++i) {
        if (clamped <= bp[i].fraction) {
            const float span = bp[i].fraction - bp[i - 1].fraction;
            const float t = span > 0.0f ? (clamped - bp[i - 1].fraction) / span : 0.0f;
            const float db = bp[i - 1].db + t * (bp[i].db - bp[i - 1].db);
            return juce::jlimit(kFaderMinDb, kFaderMaxDb, db);
        }
    }
    return kFaderMaxDb; // unreachable -- clamped can never exceed bp.back().fraction (== 1.0f)
}

} // namespace synth::ui
