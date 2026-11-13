#pragma once

#include <array>

// The band vocabulary every EQ surface shares: band shapes and cut slopes. The stage maths that uses it is in
// ParametricEQFilterDesign.h, which only the EQ's own units include.
namespace synth::eq {

/** Band shapes. The first three values are saved in old patches' slot defaults: never renumber. */
enum class BandType {
    LowShelf = 0,
    Peak = 1,
    HighShelf = 2,
    LowCut = 3,
    HighCut = 4,
    Notch = 5,
    BandPass = 6,
    Tilt = 7
};
inline constexpr int kNumBandTypes = 8;

/** Cut slopes in dB/oct, in the order of a band's Slope choice. Odd multiples of 6 add a first-order stage. */
inline constexpr std::array<int, 9> kSlopesDbPerOct = {6, 12, 18, 24, 30, 36, 48, 72, 96};
inline constexpr int kDefaultSlopeIndex = 1; // 12 dB/oct: one biquad, the band's Q as its resonance
/** Stages one band can need: 96 dB/oct is a 16th-order cut, eight biquads. */
inline constexpr int kMaxStages = 8;

} // namespace synth::eq
