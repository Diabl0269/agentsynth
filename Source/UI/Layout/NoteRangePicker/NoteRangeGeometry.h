#pragma once

#include "Music/MusicalScale.h"
#include <algorithm>
#include <cmath>
#include <optional>
#include <string>

// The note range picker's pure side: where each pitch sits on a strip that scrolls over the whole keyboard (75 white
// keys, C-1 to G9), which pitch a pointer position means, how fast the strip scrolls under a drag held at its end, and
// what each gesture does to the range. Nothing here draws and nothing needs a window. docs/layout/note-range-picker.md.
//
// Positions along the keyboard are "white-key units": the white key at index i spans [i, i + 1]. The strip shows a
// window `keys` units wide that starts at `scroll`; a unit is `keyWidth` px.
namespace synth::ui::noterange {

constexpr int kTotalWhiteKeys = 75;   ///< C-1 to G9: every MIDI pitch 0..127 and nothing above.
constexpr int kMaxVisibleKeys = 28;   ///< Four octaves, the most the strip shows.
constexpr int kMinVisibleKeys = 7;    ///< One octave, the least (a very narrow strip gets keys thinner than 7 px).
constexpr float kMinKeyWidth = 7.0f;  ///< A white key is never thinner than this when the strip has the room.
constexpr float kEdgeZoneKeys = 0.5f; ///< A drag within this many keys of the strip's end, or past it, scrolls.
constexpr float kScrollBaseKeysPerSecond = 4.0f;
constexpr float kScrollRampKeysPerSecond = 9.0f; ///< Added for every key the pointer is past the edge zone.
constexpr float kScrollMaxKeysPerSecond = 70.0f;

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

/** The index of the white key at or below `pitch` on the whole keyboard (C-1 is 0, G9 is 74). */
inline int whiteIndexOf(int pitch) noexcept { return (pitch / 12) * 7 + whiteIndexInOctave(pitch % 12); }

struct KeySpan {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float centre() const noexcept { return (x0 + x1) * 0.5f; }
};

// ---- The window --------------------------------------------------------------------------------------------

/** How many white keys a strip `width` px wide shows: as many as fit at 7 px each, between one and four octaves. */
inline int visibleKeysFor(float width) noexcept {
    return std::clamp((int)std::floor(width / kMinKeyWidth), kMinVisibleKeys, kMaxVisibleKeys);
}

inline float maxScroll(int keys) noexcept { return (float)(kTotalWhiteKeys - keys); }

/** `scroll` held to the keyboard: the strip never shows keys below C-1 or above G9. */
inline float clampScroll(float scroll, int keys) noexcept { return std::clamp(scroll, 0.0f, maxScroll(keys)); }

/** Where `pitch` sits in a strip `keyWidth` px per white key whose left edge is at `scroll`. A black key is centred on
 *  the line between its two white neighbours and 60 percent of a white key wide. */
inline KeySpan keySpan(int pitch, float scroll, float keyWidth) noexcept {
    const int index = whiteIndexOf(pitch);
    if (!isBlackKey(pitch))
        return {((float)index - scroll) * keyWidth, ((float)(index + 1) - scroll) * keyWidth};
    const float centre = ((float)(index + 1) - scroll) * keyWidth;
    return {centre - keyWidth * 0.3f, centre + keyWidth * 0.3f};
}

/** The pitch whose key centre is nearest to `x` (the lower one on a tie). `x` outside the strip means the key at the
 *  nearest end of the keyboard. */
inline int pitchAtX(float x, float scroll, float keyWidth) noexcept {
    int best = 0;
    float bestDistance = 1.0e9f;
    for (int p = 0; p < 128; ++p) {
        const float d = std::abs(keySpan(p, scroll, keyWidth).centre() - x);
        if (d < bestDistance) {
            bestDistance = d;
            best = p;
        }
    }
    return best;
}

/** The span a range covers on the whole keyboard, in white-key units: from its low key's left side to its high key's
 *  right side. */
inline KeySpan rangeExtent(int low, int high) noexcept {
    return {keySpan(low, 0.0f, 1.0f).x0, keySpan(high, 0.0f, 1.0f).x1};
}

/** True when `extent` lies inside the window. */
inline bool isInView(KeySpan extent, float scroll, int keys) noexcept {
    return extent.x0 >= scroll && extent.x1 <= scroll + (float)keys;
}

/** The scroll that centres `extent` in the window. */
inline float scrollCentredOn(KeySpan extent, int keys) noexcept {
    return clampScroll(extent.centre() - (float)keys * 0.5f, keys);
}

/** The smallest move of `scroll` that brings `extent` into the window with one key to spare at the end it came in by;
 *  `scroll` itself when it is in already. An extent wider than the window lines its left side up instead. */
inline float scrollToReveal(KeySpan extent, float scroll, int keys) noexcept {
    const float margin = 1.0f;
    if (extent.x1 - extent.x0 + 2.0f * margin > (float)keys)
        return isInView({extent.x0, extent.x0}, scroll, keys) ? scroll : clampScroll(extent.x0 - margin, keys);
    if (extent.x0 < scroll)
        return clampScroll(extent.x0 - margin, keys);
    if (extent.x1 > scroll + (float)keys)
        return clampScroll(extent.x1 + margin - (float)keys, keys);
    return scroll;
}

// ---- The overview bar --------------------------------------------------------------------------------------

/** The x, in a bar `width` px wide that shows the whole keyboard, of a position in white-key units. */
inline float overviewX(float units, float width) noexcept { return units / (float)kTotalWhiteKeys * width; }

/** The scroll that puts the window's centre under `x` in the overview bar. */
inline float overviewScrollFor(float x, float width, int keys) noexcept {
    if (width <= 0.0f)
        return 0.0f;
    return clampScroll(x / width * (float)kTotalWhiteKeys - (float)keys * 0.5f, keys);
}

// ---- Scrolling under a drag --------------------------------------------------------------------------------

/** How fast the strip scrolls, in white keys per second (negative: toward lower notes), while a drag holds the pointer
 *  at `x` in a strip `width` px wide. Zero away from the ends; within half a key of an end, or past it, it starts at 4
 *  keys a second and grows by 9 for every key further out, up to 70. */
inline float autoScrollSpeed(float x, float width, float keyWidth) noexcept {
    if (keyWidth <= 0.0f)
        return 0.0f;
    const float zone = keyWidth * kEdgeZoneKeys;
    float overshootKeys = 0.0f;
    float direction = 0.0f;
    if (x < zone) {
        overshootKeys = (zone - x) / keyWidth;
        direction = -1.0f;
    } else if (x > width - zone) {
        overshootKeys = (x - (width - zone)) / keyWidth;
        direction = 1.0f;
    } else {
        return 0.0f;
    }
    return direction *
           std::min(kScrollBaseKeysPerSecond + overshootKeys * kScrollRampKeysPerSecond, kScrollMaxKeysPerSecond);
}

// ---- What each gesture does to the range -------------------------------------------------------------------

struct Range {
    int lo = 0;
    int hi = 0;
    bool operator==(const Range&) const = default;
};

/** The note an edge dragged to `x` lands on: the key under the pointer (the low edge's left side, the high edge's
 *  right side), then the nearest scale note. */
inline int edgePitchAt(bool lowEdge, float x, float scroll, float keyWidth, const MusicalScale* scale) noexcept {
    const float half = keyWidth * 0.5f;
    int pitch = pitchAtX(lowEdge ? x + half : x - half, scroll, keyWidth);
    if (scale != nullptr)
        pitch = scale->snapPitch(pitch);
    return pitch;
}

/** How many scale notes lie after `from` up to and including `to` (negative when `to` is below `from`: minus the
 *  count in [to, from)). Every semitone counts without a scale. */
inline int scaleStepsBetween(int from, int to, const MusicalScale* scale) noexcept {
    if (scale == nullptr)
        return to - from;
    int steps = 0;
    for (int p = from + 1; p <= to; ++p)
        steps += scale->contains(p) ? 1 : 0;
    for (int p = to; p < from; ++p)
        steps -= scale->contains(p) ? 1 : 0;
    return steps;
}

/** The range after the middle is dragged: it follows the pointer's note, so `grabPitch` (the note under the pointer
 *  when the drag began) goes to `pointerPitch`. By one note, or one scale note with a scale; with `octaves`, whole
 *  octaves (the shift rounded to the nearest octave). It stops at the ends of the keyboard, keeping its width. */
inline Range dragBandRange(int startLow, int startHigh, int grabPitch, int pointerPitch, bool octaves,
                           const MusicalScale* scale) noexcept {
    if (octaves) {
        int shift = (int)std::lround((double)(pointerPitch - grabPitch) / 12.0) * 12;
        shift = std::clamp(shift, -startLow, 127 - startHigh);
        shift = shift / 12 * 12;
        return {startLow + shift, startHigh + shift};
    }
    if (scale == nullptr) {
        const int shift = std::clamp(pointerPitch - grabPitch, -startLow, 127 - startHigh);
        return {startLow + shift, startHigh + shift};
    }
    const int steps = scaleStepsBetween(grabPitch, pointerPitch, scale);
    const int direction = steps < 0 ? -1 : 1;
    int lo = startLow;
    int hi = startHigh;
    for (int i = 0; i < std::abs(steps); ++i) {
        const int nextLo = scale->stepBy(lo, direction);
        const int nextHi = scale->stepBy(hi, direction);
        if (nextLo == lo || nextHi == hi)
            break;
        lo = nextLo;
        hi = nextHi;
    }
    return {lo, hi};
}

/** The range after a click on empty space at `pitch`: the same width, centred there, held to the keyboard. With a scale
 *  it keeps the same number of scale notes and its edges are scale notes. */
inline Range bandCentredOn(int pitch, int low, int high, const MusicalScale* scale) noexcept {
    const int width = high - low;
    int lo = std::clamp(pitch - width / 2, 0, 127 - width);
    if (scale == nullptr)
        return {lo, lo + width};
    const int notes = scaleStepsBetween(low, high, scale);
    lo = scale->snapPitch(lo);
    int hi = scale->stepBy(lo, notes);
    // At the top of the keyboard the scale may run out before the width does: slide down until it all fits.
    while (hi - lo < width && lo > 0 && scaleStepsBetween(lo, hi, scale) < notes) {
        lo = scale->stepBy(lo, -1);
        hi = scale->stepBy(lo, notes);
    }
    return {lo, std::max(lo, hi)};
}

// ---- Typing a note -----------------------------------------------------------------------------------------

/** The pitch a typed key name means: a letter A to G, an optional # or b, an optional minus and the octave number,
 *  as "C#3" or "Bb-1" (case of the letter does not matter; `middleCOctave` is the octave MIDI 60 is in). Nothing when
 *  it is not such a name or the pitch is outside 0..127. */
inline std::optional<int> parseNoteName(const std::string& text, int middleCOctave) noexcept {
    size_t i = 0;
    while (i < text.size() && text[i] == ' ')
        ++i;
    if (i >= text.size())
        return std::nullopt;
    static constexpr int kBase[7] = {9, 11, 0, 2, 4, 5, 7}; // A B C D E F G
    const char letter = text[i];
    const char upper = (char)((letter >= 'a' && letter <= 'g') ? letter - 'a' + 'A' : letter);
    if (upper < 'A' || upper > 'G')
        return std::nullopt;
    int pitchClass = kBase[upper - 'A'];
    ++i;
    if (i < text.size() && (text[i] == '#' || text[i] == 'b')) {
        pitchClass += text[i] == '#' ? 1 : -1;
        ++i;
    }
    bool negative = false;
    if (i < text.size() && text[i] == '-') {
        negative = true;
        ++i;
    }
    if (i >= text.size() || text[i] < '0' || text[i] > '9')
        return std::nullopt;
    int octave = 0;
    for (; i < text.size() && text[i] >= '0' && text[i] <= '9'; ++i) {
        octave = octave * 10 + (text[i] - '0');
        if (octave > 99)
            return std::nullopt;
    }
    while (i < text.size() && text[i] == ' ')
        ++i;
    if (i != text.size())
        return std::nullopt;
    if (negative)
        octave = -octave;
    const int pitch = (octave + 5 - middleCOctave) * 12 + pitchClass;
    if (pitch < 0 || pitch > 127)
        return std::nullopt;
    return pitch;
}

} // namespace synth::ui::noterange
