#pragma once

#include "Music/MusicalScale.h"
#include "SequencerPattern.h"
#include <array>
#include <cstdint>

namespace synth {

/** Deterministic 64-bit mix of a pattern seed and a salt: the one source of "randomness" in playback, so two
 *  renders of the same project are identical. */
std::uint64_t sequencerHash(std::uint32_t seed, std::uint64_t value) noexcept;

/** The notes of one chord hit, in MIDI pitch order of construction (root first). */
struct SequencerChordNotes {
    std::array<int, 8> notes{};
    int count = 0;
};

/** The notes a step plays. `snappedRoot` is already through the scale; legacy and fixed chords stack their intervals
 *  on it unsnapped, scale chords stack scale notes. Pitches outside 0..127 are dropped. `stepIndex` feeds the Random
 *  chord's hash. Allocation-free. */
SequencerChordNotes sequencerChordNotes(SequencerChord chord, int snappedRoot, const MusicalScale& scale,
                                        std::uint32_t seed, std::int64_t stepIndex) noexcept;

} // namespace synth
