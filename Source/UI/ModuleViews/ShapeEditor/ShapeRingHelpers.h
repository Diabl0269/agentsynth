#pragma once

// ShapeRingHelpers.h -- the curves the Ring helpers menu stamps on a Filter shape: narrow, tall peaks on the harmonics
// of a note, on the notes of a chord, or an octave apart, over a low floor, so a resonant filter rings at those
// pitches. Pure data: the shape window turns each into one undo step.
#include "Modules/Lfo/LfoCustomWave.h"
#include "Music/MusicalScale.h"
#include <optional>
#include <vector>

namespace synth::ui::ringhelpers {

/** The Ring helpers menu's items, in menu order (the value is the item id). */
enum class Helper { tunePeak = 1, stackHarmonics, chordPeaks, octaveBells };
constexpr Helper kHelpers[] = {Helper::tunePeak, Helper::stackHarmonics, Helper::chordPeaks, Helper::octaveBells};
juce::String helperName(Helper helper);
juce::String helperTooltip(Helper helper);

/** One peak: where (hertz) and how tall (dB). */
struct Peak {
    double hz = 440.0;
    float db = 12.0f;
};

/** Every peak's level sits this far over the floor at most; the floor is where the rest of the spectrum is kept. */
constexpr float kFloorDb = -12.0f;
constexpr float kTopDb = 12.0f;
constexpr int kHarmonics = 8;
constexpr float kHarmonicStepDb = 2.0f;
/** Half a peak's width at its foot, in octaves (a narrow peak: a sixth of an octave across). */
constexpr double kHalfWidthOctaves = 1.0 / 12.0;
constexpr double kBellHalfWidthOctaves = 1.0 / 24.0;
/** The Stack harmonics panel's Width choices (Narrow, Medium, Wide), as the half width in octaves; Medium is the
 *  default. */
constexpr double kStackWidthOctaves[] = {1.0 / 24.0, kHalfWidthOctaves, 1.0 / 6.0};

/** What the Stack harmonics panel asks for. The defaults are what the menu item used before it had a panel. */
struct StackOptions {
    double rootHz = 110.0;
    int count = kHarmonics;
    float stepDb = kHarmonicStepDb;
    double halfWidthOctaves = kHalfWidthOctaves;
};

/** The first `count` multiples of `rootHz` inside the filter's range, each `stepDb` quieter than the one before. */
std::vector<Peak> stackHarmonics(double rootHz, int count = kHarmonics, float topDb = kTopDb,
                                 float stepDb = kHarmonicStepDb);
/** The curve for the panel's options: `options.count` harmonics of the root, each `stepDb` quieter. */
LfoCustomWave stackCurve(const StackOptions& options);
/** Root, third and fifth of `key` (A minor's are A, C, E) in the octaves of C3 and C4; no key: A major. The third is
 *  the key's (minor when it has a minor third and no major one). */
std::vector<Peak> chordPeaks(const std::optional<MusicalScale>& key);
/** `rootHz` and its octaves up to 10 kHz, all at full height. */
std::vector<Peak> octaveBells(double rootHz);

/** A flat curve at kFloorDb with a triangle peak at each peak (half width `halfWidthOctaves`, narrowed where two
 *  would overlap). At most (LfoCustomWave::kMaxPoints - 2) / 3 peaks are kept, lowest first. */
LfoCustomWave curveWithPeaks(std::vector<Peak> peaks, double halfWidthOctaves = kHalfWidthOctaves);
/** Moves point `index` of `curve` (-1: its highest inner point) across to the nearest note's frequency, a note of
 *  `key` when there is one, staying between its neighbours. nullopt when the curve has no inner point to move. */
std::optional<LfoCustomWave> tunePeak(const LfoCustomWave& curve, int index, const std::optional<MusicalScale>& key);

} // namespace synth::ui::ringhelpers
