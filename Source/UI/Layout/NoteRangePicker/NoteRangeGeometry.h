#pragma once

#include <algorithm>
#include <cmath>

// The note range picker's pure side: a strip of four octaves (28 white keys) starting at a C, where each pitch sits,
// which pitch a pointer position means, and which octaves to show for a range. Nothing here draws.
// docs/layout/note-range-picker.md.
namespace synth::ui::noterange {

constexpr int kWhiteKeys = 28;
constexpr int kSpan = 48;    ///< Semitones shown (four octaves).
constexpr int kMaxBase = 84; ///< The highest first C: 84 + 48 covers every MIDI pitch.

inline bool isBlackKey(int pitch) noexcept {
    const int pc = pitch % 12;
    return pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10;
}

/** How many white keys lie left of the white key at or below `pitchClass` (0..11): C 0, D 1, E 2, F 3, G 4, A 5, B 6.
 */
inline int whiteIndexInOctave(int pitchClass) noexcept {
    static constexpr int kIndex[12] = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
    return kIndex[pitchClass];
}

struct KeySpan {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float centre() const noexcept { return (x0 + x1) * 0.5f; }
};

/** Where `pitch` sits in a strip `width` px wide whose first key is the C at `base`. A black key is centred on the
 *  line between its two white neighbours and 60 percent of a white key wide. A pitch outside the strip lies outside
 *  [0, width]. */
inline KeySpan keySpan(int pitch, int base, float width) noexcept {
    const float white = width / (float)kWhiteKeys;
    const int rel = pitch - base;
    const int octave = (int)std::floor((double)rel / 12.0);
    const int pc = rel - octave * 12;
    const int whiteIndex = octave * 7 + whiteIndexInOctave(pc);
    if (!isBlackKey(pc))
        return {white * (float)whiteIndex, white * (float)(whiteIndex + 1)};
    const float centre = white * (float)(whiteIndex + 1);
    return {centre - white * 0.3f, centre + white * 0.3f};
}

/** The pitch whose key centre is nearest to `x`, clamped to the strip. */
inline int pitchAtX(float x, int base, float width) noexcept {
    int best = base;
    float bestDistance = 1.0e9f;
    for (int p = base; p < base + kSpan; ++p) {
        const float d = std::abs(keySpan(p, base, width).centre() - x);
        if (d < bestDistance) {
            bestDistance = d;
            best = p;
        }
    }
    return std::clamp(best, 0, 127);
}

/** The first C of the strip that shows a range: `current` when the range sits inside it with at least two semitones
 *  to spare at each end that can still scroll, else the strip centred on the range. */
inline int windowBaseFor(int low, int high, int current) noexcept {
    const int lastShown = current + kSpan - 1;
    const bool roomLeft = low - current >= 2 || current == 0;
    const bool roomRight = lastShown - high >= 2 || current >= kMaxBase;
    if (low >= current && high <= lastShown && roomLeft && roomRight)
        return current;
    const double centre = (double)(low + high) * 0.5;
    const int base = 12 * (int)std::lround((centre - (double)kSpan * 0.5) / 12.0);
    return std::clamp(base, 0, kMaxBase);
}

} // namespace synth::ui::noterange
