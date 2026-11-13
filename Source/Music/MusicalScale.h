#pragma once

// Pure music theory shared by the piano roll's scale assist and both sequencers: scale
// membership, nearest-pitch snapping, stepping/degree maths, an audio-thread snap table, built-in
// scale presets, user-scale persistence, random pitch picking, and in-scale chords with names and
// roman numerals. Header-only and deliberately free of any Timeline, UI or module type (only
// juce_core), so every consumer can include it; code that needs MidiNote lives in
// Timeline/ScaleNoteGenerator.h.

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

// A scale is a root pitch class (0=C .. 11=B) plus a 12-bit interval mask: bit i set means
// "root + i semitones (mod 12) is in the scale". The mask is root-relative so the same mask
// value describes the scale shape at any root (e.g. Major = 0xAB5 regardless of key).
struct MusicalScale {
    int rootPitchClass = 0;
    std::uint16_t mask = 0xFFF; // default: chromatic (all 12 bits set)
    juce::String name;

    bool isChromatic() const noexcept { return (mask & 0xFFF) == 0xFFF; }

    bool operator==(const MusicalScale& other) const noexcept {
        return rootPitchClass == other.rootPitchClass && mask == other.mask && name == other.name;
    }

    // Degenerate mask (0) contains nothing — every pitch is "out of scale", which callers must
    // treat as "no candidate pitch" rather than crashing or looping.
    bool contains(int midiPitch) const noexcept {
        if (mask == 0)
            return false;
        int interval = ((midiPitch - rootPitchClass) % 12 + 12) % 12;
        return (mask & (1u << interval)) != 0;
    }

    // Nearest in-scale pitch within [0, 127]. Ties (equidistant above/below) resolve to the
    // LOWER pitch. mask == 0 has no in-scale pitch at all, so it passes the input through
    // unchanged rather than searching forever.
    int snapPitch(int midiPitch) const noexcept {
        if (mask == 0)
            return midiPitch;
        int clamped = std::clamp(midiPitch, 0, 127);
        if (contains(clamped))
            return clamped;
        for (int distance = 1; distance <= 127; ++distance) {
            int lower = clamped - distance;
            int upper = clamped + distance;
            bool lowerOk = lower >= 0 && contains(lower);
            bool upperOk = upper <= 127 && contains(upper);
            if (lowerOk && upperOk)
                return lower; // tie -> lower pitch wins
            if (lowerOk)
                return lower;
            if (upperOk)
                return upper;
        }
        return midiPitch; // no in-scale pitch anywhere in range (mask has bits but none reachable — cannot happen for a
                          // non-zero mask, kept as a safe fallback)
    }

    // Number of scale notes per octave (popcount of the 12-bit mask).
    int notesPerOctave() const noexcept {
        int count = 0;
        for (int bit = 0; bit < 12; ++bit)
            count += (mask & (1u << bit)) != 0 ? 1 : 0;
        return count;
    }

    // Snaps the pitch first, then moves |scaleSteps| in-scale notes up (positive) or down
    // (negative), stopping at the last in-scale pitch inside [0, 127]. mask == 0 returns the input
    // clamped; chromatic is a plain clamped semitone add.
    int stepBy(int midiPitch, int scaleSteps) const noexcept {
        if (mask == 0)
            return std::clamp(midiPitch, 0, 127);
        if (isChromatic())
            return std::clamp(std::clamp(midiPitch, 0, 127) + scaleSteps, 0, 127);
        int pitch = snapPitch(midiPitch);
        const int direction = scaleSteps >= 0 ? 1 : -1;
        int remaining = scaleSteps >= 0 ? scaleSteps : -scaleSteps;
        while (remaining > 0) {
            int next = pitch + direction;
            while (next >= 0 && next <= 127 && !contains(next))
                next += direction;
            if (next < 0 || next > 127)
                break;
            pitch = next;
            --remaining;
        }
        return pitch;
    }

    // 0-based index of the snapped pitch's pitch class among the scale's pitch classes counted
    // upward from the root (root = 0). mask == 0 has no degrees: -1.
    int degreeOf(int midiPitch) const noexcept {
        if (mask == 0)
            return -1;
        const int snapped = snapPitch(midiPitch);
        const int interval = ((snapped - rootPitchClass) % 12 + 12) % 12;
        int degree = 0;
        for (int bit = 0; bit < interval; ++bit)
            degree += (mask & (1u << bit)) != 0 ? 1 : 0;
        return degree;
    }

    // Entry i = snapPitch(i). For audio-thread lookup: build it on the message thread or once per
    // change, never per sample.
    std::array<std::int8_t, 128> snapTable() const noexcept {
        std::array<std::int8_t, 128> table{};
        for (int i = 0; i < 128; ++i)
            table[(size_t)i] = (std::int8_t)std::clamp(snapPitch(i), 0, 127);
        return table;
    }
};

// Root-relative interval masks for the built-in scale library. Bit 0 (root itself) is always
// set for every real scale; only the degenerate/unused mask 0 has it clear.
struct ScalePreset {
    const char* name;
    std::uint16_t mask;
};

inline const std::vector<ScalePreset>& builtInScalePresets() {
    static const std::vector<ScalePreset> presets = {
        {"Major", 0x0AB5},             // 0,2,4,5,7,9,11
        {"Natural Minor", 0x05AD},     // 0,2,3,5,7,8,10
        {"Harmonic Minor", 0x09AD},    // 0,2,3,5,7,8,11
        {"Melodic Minor", 0x0AAD},     // 0,2,3,5,7,9,11
        {"Dorian", 0x06AD},            // 0,2,3,5,7,9,10
        {"Phrygian", 0x05AB},          // 0,1,3,5,7,8,10
        {"Phrygian Dominant", 0x05B3}, // 0,1,4,5,7,8,10 (5th mode of harmonic minor) — grouped next
                                       // to Phrygian rather than appended, since presets are read
                                       // top-to-bottom as "the modes" followed by "everything else".
        {"Lydian", 0x0AD5},            // 0,2,4,6,7,9,11
        {"Mixolydian", 0x06B5},        // 0,2,4,5,7,9,10
        {"Locrian", 0x056B},           // 0,1,3,5,6,8,10
        {"Major Pentatonic", 0x0295},  // 0,2,4,7,9
        {"Minor Pentatonic", 0x04A9},  // 0,3,5,7,10
        {"Blues", 0x04E9},             // 0,3,5,6,7,10
        {"Whole Tone", 0x0555},        // 0,2,4,6,8,10
        {"Chromatic", 0x0FFF},         // all 12
    };
    return presets;
}

// Builds a scale from a built-in preset by index (clamped into range) at the given root.
inline MusicalScale makeScale(int rootPitchClass, int presetIndex) {
    const auto& presets = builtInScalePresets();
    int index = std::clamp(presetIndex, 0, (int)presets.size() - 1);
    MusicalScale scale;
    scale.rootPitchClass = ((rootPitchClass % 12) + 12) % 12;
    scale.mask = presets[(size_t)index].mask;
    scale.name = presets[(size_t)index].name;
    return scale;
}

// User-defined scale, persisted by the UI under one ApplicationProperties key as a JSON array.
struct UserScale {
    juce::String name;
    std::uint16_t mask = 0;
};

// Tolerant parse: malformed entries are skipped individually, and a whole-string parse
// failure (not valid JSON at all) yields an empty list rather than throwing or crashing —
// this is user-edited/settings-file data, never trusted the way validatePatch's input is.
inline std::vector<UserScale> parseUserScales(const juce::String& json) {
    std::vector<UserScale> result;
    if (json.trim().isEmpty())
        return result;

    juce::var parsed;
    if (juce::JSON::parse(json, parsed).failed())
        return result;
    if (!parsed.isArray())
        return result;

    for (auto& entry : *parsed.getArray()) {
        if (!entry.isObject())
            continue;
        auto* obj = entry.getDynamicObject();
        if (obj == nullptr)
            continue;
        if (!obj->hasProperty("name") || !obj->hasProperty("mask"))
            continue;
        juce::var nameVar = obj->getProperty("name");
        juce::var maskVar = obj->getProperty("mask");
        if (!nameVar.isString() || !(maskVar.isInt() || maskVar.isInt64() || maskVar.isDouble()))
            continue;

        int maskValue = (int)maskVar;
        if (maskValue < 0 || maskValue > 0xFFF)
            continue;

        UserScale scale;
        scale.name = nameVar.toString();
        scale.mask = (std::uint16_t)maskValue;
        result.push_back(scale);
    }
    return result;
}

inline juce::String serializeUserScales(const std::vector<UserScale>& scales) {
    juce::Array<juce::var> array;
    for (auto& scale : scales) {
        auto obj = new juce::DynamicObject();
        obj->setProperty("name", scale.name);
        obj->setProperty("mask", (int)scale.mask);
        array.add(juce::var(obj));
    }
    return juce::JSON::toString(juce::var(array));
}

// Builds a scale from an explicit root-relative mask (low 12 bits used).
inline MusicalScale makeScaleFromMask(int rootPitchClass, std::uint16_t mask, juce::String name = {}) {
    MusicalScale scale;
    scale.rootPitchClass = ((rootPitchClass % 12) + 12) % 12;
    scale.mask = (std::uint16_t)(mask & 0xFFF);
    scale.name = std::move(name);
    return scale;
}

// Picks `count` pitches uniformly among the in-scale pitches inside [minPitch, maxPitch] (every
// pitch in range when scale is null or chromatic). Empty when no candidate exists. One
// rng.nextInt(candidates.size()) per pick, so a seeded rng gives a reproducible sequence.
inline std::vector<int> pickPitches(int count, int minPitch, int maxPitch, const MusicalScale* scale,
                                    juce::Random& rng) {
    std::vector<int> picks;
    int lo = std::clamp(std::min(minPitch, maxPitch), 0, 127);
    int hi = std::clamp(std::max(minPitch, maxPitch), 0, 127);

    std::vector<int> candidates;
    for (int pitch = lo; pitch <= hi; ++pitch) {
        if (scale == nullptr || scale->isChromatic() || scale->contains(pitch))
            candidates.push_back(pitch);
    }
    if (candidates.empty() || count <= 0)
        return picks;

    picks.reserve((size_t)count);
    for (int i = 0; i < count; ++i)
        picks.push_back(candidates[(size_t)rng.nextInt((int)candidates.size())]);
    return picks;
}

//==============================================================================
// Chords
//==============================================================================

enum class ScaleChordKind { Triad, Seventh, Sus2, Sus4, Add9, Sixth };

// Up to six MIDI notes, ascending, notes[0] == rootPitch. `count` entries are valid.
struct ScaleChord {
    int rootPitch = 0;
    std::array<int, 6> notes{};
    int count = 0;
};

namespace detail {

// The in-scale pitch `offset` scale notes above `pitch` (offset 0 is `pitch` itself). Returns -1
// past 127. `pitch` must be in the scale.
inline int scaleNoteAbove(const MusicalScale& scale, int pitch, int offset) noexcept {
    int current = pitch;
    for (int i = 0; i < offset; ++i) {
        int next = current + 1;
        while (next <= 127 && !scale.contains(next))
            ++next;
        if (next > 127)
            return -1;
        current = next;
    }
    return current;
}

} // namespace detail

// A chord of stacked scale notes: Triad 0,2,4; Seventh 0,2,4,6; Sus2 0,1,4; Sus4 0,3,4;
// Add9 0,2,4,(notesPerOctave+1); Sixth 0,2,4,5 (offsets in scale notes above the snapped root,
// continuing across octaves). Notes above 127 are dropped. A chromatic or empty scale falls back to
// the Major scale at the root's pitch class, so Triad is a major triad, Seventh a maj7, etc.
inline ScaleChord chordInScale(const MusicalScale& scale, int rootPitch, ScaleChordKind kind) noexcept {
    MusicalScale effective = scale;
    int root = std::clamp(rootPitch, 0, 127);
    if (scale.mask == 0 || scale.isChromatic())
        effective = makeScaleFromMask(root % 12, 0x0AB5);
    else
        root = scale.snapPitch(root);

    std::array<int, 4> offsets{};
    switch (kind) {
    case ScaleChordKind::Triad:
        offsets = {0, 2, 4, -1};
        break;
    case ScaleChordKind::Seventh:
        offsets = {0, 2, 4, 6};
        break;
    case ScaleChordKind::Sus2:
        offsets = {0, 1, 4, -1};
        break;
    case ScaleChordKind::Sus4:
        offsets = {0, 3, 4, -1};
        break;
    case ScaleChordKind::Add9:
        offsets = {0, 2, 4, effective.notesPerOctave() + 1};
        break;
    case ScaleChordKind::Sixth:
        offsets = {0, 2, 4, 5};
        break;
    }

    ScaleChord chord;
    chord.rootPitch = root;
    for (int offset : offsets) {
        if (offset < 0)
            break;
        const int pitch = detail::scaleNoteAbove(effective, root, offset);
        if (pitch < 0)
            break; // offsets ascend, so everything after is above 127 too
        chord.notes[(size_t)chord.count++] = pitch;
    }
    return chord;
}

// A chord from explicit semitone offsets above rootPitch (for chords outside the key). Notes
// outside [0, 127] are dropped; at most six are kept.
inline ScaleChord chordFromIntervals(int rootPitch, const std::vector<int>& semitones) {
    ScaleChord chord;
    chord.rootPitch = std::clamp(rootPitch, 0, 127);
    for (int semitone : semitones) {
        const int pitch = chord.rootPitch + semitone;
        if (pitch < 0 || pitch > 127 || chord.count >= 6)
            continue;
        chord.notes[(size_t)chord.count++] = pitch;
    }
    return chord;
}

inline ScaleChord chordFromIntervals(int rootPitch, std::initializer_list<int> semitones) {
    return chordFromIntervals(rootPitch, std::vector<int>(semitones));
}

namespace detail {

// One row per known chord quality, keyed by the semitone set above the root. `numeralSuffix` is
// appended to the roman numeral; `lowerCase` (minor third) picks a lower-case numeral.
struct ChordQuality {
    std::vector<int> intervals;
    const char* nameSuffix;
    juce::String numeralSuffix;
    bool lowerCase;
};

inline const std::vector<ChordQuality>& chordQualities() {
    static const juce::String degree = juce::String::fromUTF8("\xC2\xB0");  // degree sign, as in a diminished vii
    static const juce::String halfDim = juce::String::fromUTF8("\xC3\xB8"); // o-slash, as in a half-diminished vii7
    static const std::vector<ChordQuality> table = {
        {{0, 4, 7}, "", "", false},
        {{0, 3, 7}, "m", "", true},
        {{0, 3, 6}, "dim", degree, true},
        {{0, 4, 8}, "aug", "+", false},
        {{0, 2, 7}, "sus2", "sus2", false},
        {{0, 5, 7}, "sus4", "sus4", false},
        {{0, 4, 7, 11}, "maj7", "maj7", false},
        {{0, 4, 7, 10}, "7", "7", false},
        {{0, 3, 7, 10}, "m7", "7", true},
        {{0, 3, 6, 10}, "m7b5", halfDim + "7", true},
        {{0, 3, 6, 9}, "dim7", degree + "7", true},
        {{0, 3, 7, 11}, "mMaj7", "maj7", true},
        {{0, 4, 8, 11}, "maj7#5", "+maj7", false},
        {{0, 4, 7, 14}, "add9", "add9", false},
        {{0, 3, 7, 14}, "madd9", "add9", true},
        {{0, 4, 7, 9}, "6", "6", false},
        {{0, 3, 7, 9}, "m6", "6", true},
        {{0}, "", "", false},
        {{0, 7}, "5", "5", false},
        {{0, 12}, "", "", false},
    };
    return table;
}

// Semitones above the root, ascending.
inline std::vector<int> chordIntervals(const ScaleChord& chord) {
    std::vector<int> out;
    for (int i = 0; i < chord.count; ++i)
        out.push_back(chord.notes[(size_t)i] - chord.rootPitch);
    std::sort(out.begin(), out.end());
    return out;
}

// Exact match first; otherwise compound intervals above an octave are reduced mod 12 (except the
// 9th, 14) and duplicates dropped, then matched again.
inline const ChordQuality* findChordQuality(std::vector<int> intervals) {
    auto matches = [](const std::vector<int>& candidate) -> const ChordQuality* {
        for (auto& row : chordQualities())
            if (row.intervals == candidate)
                return &row;
        return nullptr;
    };
    if (auto* exact = matches(intervals))
        return exact;
    for (size_t i = 1; i < intervals.size(); ++i)
        if (intervals[i] > 12 && intervals[i] != 14)
            intervals[i] %= 12;
    std::sort(intervals.begin(), intervals.end());
    intervals.erase(std::unique(intervals.begin(), intervals.end()), intervals.end());
    return matches(intervals);
}

inline const char* sharpNoteName(int pitchClass) noexcept {
    static const char* const names[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return names[((pitchClass % 12) + 12) % 12];
}

} // namespace detail

// Root note name (sharps) plus quality suffix, e.g. "C#", "Dm7", "Bm7b5". Unknown shapes print the
// root and the intervals: "C(0,1,2)".
inline juce::String chordName(const ScaleChord& chord) {
    juce::String root = detail::sharpNoteName(chord.rootPitch);
    const auto intervals = detail::chordIntervals(chord);
    if (auto* quality = detail::findChordQuality(intervals))
        return root + quality->nameSuffix;
    juce::StringArray parts;
    for (int interval : intervals)
        parts.add(juce::String(interval));
    return root + "(" + parts.joinIntoString(",") + ")";
}

// Roman numeral of the chord relative to the scale: I..VII by scale degree for 7-note scales whose
// notes include the chord root; otherwise the chromatic distance from the scale root with Major
// degree names and a b/# prefix (bVII). Upper case for a major third / sus / power chords, lower
// case for a minor third; quality and extension suffixes follow (vii°, ii7, V7, Imaj7).
inline juce::String romanNumeral(const MusicalScale& scale, const ScaleChord& chord) {
    static const char* const diatonic[7] = {"I", "II", "III", "IV", "V", "VI", "VII"};
    static const char* const chromatic[12] = {"I",   "bII", "II",  "bIII", "III",  "IV",
                                              "#IV", "V",   "bVI", "VI",   "bVII", "VII"};
    juce::String numeral;
    if (scale.mask != 0 && scale.notesPerOctave() == 7 && scale.contains(chord.rootPitch))
        numeral = diatonic[std::clamp(scale.degreeOf(chord.rootPitch), 0, 6)];
    else
        numeral = chromatic[((chord.rootPitch - scale.rootPitchClass) % 12 + 12) % 12];

    const auto intervals = detail::chordIntervals(chord);
    const auto* quality = detail::findChordQuality(intervals);
    bool lowerCase = false;
    juce::String suffix;
    if (quality != nullptr) {
        lowerCase = quality->lowerCase;
        suffix = quality->numeralSuffix;
    } else {
        const bool hasMinorThird = std::find(intervals.begin(), intervals.end(), 3) != intervals.end();
        const bool hasMajorThird = std::find(intervals.begin(), intervals.end(), 4) != intervals.end();
        lowerCase = hasMinorThird && !hasMajorThird;
    }
    if (lowerCase) {
        // Lower-case the letters only, keeping a leading b/# accidental as is.
        const int skip = (numeral.startsWithChar('b') || numeral.startsWithChar('#')) ? 1 : 0;
        numeral = numeral.substring(0, skip) + numeral.substring(skip).toLowerCase();
    }
    return numeral + suffix;
}

} // namespace synth
