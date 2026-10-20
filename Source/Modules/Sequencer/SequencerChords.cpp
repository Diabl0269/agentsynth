#include "SequencerChords.h"

namespace synth {

namespace {

struct IntervalSet {
    int count;
    std::array<int, 4> semitones;
};

// Intervals above the root for every chord that is not built from the scale or the hash.
IntervalSet fixedIntervals(SequencerChord chord) noexcept {
    switch (chord) {
    case SequencerChord::Major:
        return {3, {0, 4, 7, 0}};
    case SequencerChord::Minor:
        return {3, {0, 3, 7, 0}};
    case SequencerChord::Maj7:
        return {4, {0, 4, 7, 11}};
    case SequencerChord::Min7:
        return {4, {0, 3, 7, 10}};
    case SequencerChord::Fifths:
        return {2, {0, 7, 0, 0}};
    case SequencerChord::Octaves:
        return {2, {0, 12, 0, 0}};
    case SequencerChord::Dom7:
        return {4, {0, 4, 7, 10}};
    case SequencerChord::Dim:
        return {3, {0, 3, 6, 0}};
    case SequencerChord::Aug:
        return {3, {0, 4, 8, 0}};
    case SequencerChord::Sus2:
        return {3, {0, 2, 7, 0}};
    case SequencerChord::Sus4:
        return {3, {0, 5, 7, 0}};
    case SequencerChord::Sixth:
        return {4, {0, 4, 7, 9}};
    case SequencerChord::Min6:
        return {4, {0, 3, 7, 9}};
    case SequencerChord::Add9:
        return {4, {0, 4, 7, 14}};
    case SequencerChord::Dim7:
        return {4, {0, 3, 6, 9}};
    case SequencerChord::HalfDim7:
        return {4, {0, 3, 6, 10}};
    case SequencerChord::MinMaj7:
        return {4, {0, 3, 7, 11}};
    default:
        return {1, {0, 0, 0, 0}};
    }
}

bool scaleKind(SequencerChord chord, ScaleChordKind& kind) noexcept {
    switch (chord) {
    case SequencerChord::ScaleTriad:
        kind = ScaleChordKind::Triad;
        return true;
    case SequencerChord::ScaleSeventh:
        kind = ScaleChordKind::Seventh;
        return true;
    case SequencerChord::ScaleSus2:
        kind = ScaleChordKind::Sus2;
        return true;
    case SequencerChord::ScaleSus4:
        kind = ScaleChordKind::Sus4;
        return true;
    case SequencerChord::ScaleAdd9:
        kind = ScaleChordKind::Add9;
        return true;
    case SequencerChord::ScaleSixth:
        kind = ScaleChordKind::Sixth;
        return true;
    default:
        return false;
    }
}

// Out-of-range and repeated pitches are dropped, so a note is never started twice.
void add(SequencerChordNotes& out, int pitch) noexcept {
    if (pitch < 0 || pitch > 127 || out.count >= (int)out.notes.size())
        return;
    for (int i = 0; i < out.count; ++i)
        if (out.notes[(size_t)i] == pitch)
            return;
    out.notes[(size_t)out.count++] = pitch;
}

} // namespace

std::uint64_t sequencerHash(std::uint32_t seed, std::uint64_t value) noexcept {
    std::uint64_t x = ((std::uint64_t)seed << 32) ^ (value * 0x9E3779B97F4A7C15ull) ^ 0xD1B54A32D192ED03ull;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return x;
}

SequencerChordNotes sequencerChordNotes(SequencerChord chord, int snappedRoot, const MusicalScale& scale,
                                        std::uint32_t seed, std::int64_t stepIndex) noexcept {
    SequencerChordNotes out;
    ScaleChordKind kind = ScaleChordKind::Triad;
    if (scaleKind(chord, kind)) {
        const auto built = chordInScale(scale, snappedRoot, kind);
        for (int i = 0; i < built.count; ++i)
            add(out, built.notes[(size_t)i]);
    } else if (chord == SequencerChord::Random) {
        // Today's three notes: the root, one up to 11 semitones above and one up to 11 below.
        const auto bits = sequencerHash(seed, (std::uint64_t)stepIndex * 2 + 1);
        add(out, snappedRoot);
        add(out, snappedRoot + (int)(bits % 12));
        add(out, snappedRoot - (int)((bits >> 20) % 12));
    } else {
        const auto set = fixedIntervals(chord);
        for (int i = 0; i < set.count; ++i)
            add(out, snappedRoot + set.semitones[(size_t)i]);
    }
    return out;
}

} // namespace synth
