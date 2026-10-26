#pragma once

// The song key's music theory: a key is a MusicalScale (root + mask + name). Display names, the saved form, the
// search the key picker runs ("d dor"), relative major/minor and key detection from a pitch-class histogram.
// Header-only and juce_core only, like MusicalScale.h; the song key itself lives in TransportDoc.

#include "Music/MusicalScale.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <optional>
#include <utility>
#include <vector>

namespace synth {

/** The settings key the user's own scales (UserScale JSON) are saved under, shared by every scale and key picker. */
inline constexpr const char* kUserScalesSettingsKey = "pianoRollUserScales";

/** Root name with sharps ("C#"), for any integer pitch class. */
inline juce::String keyRootName(int pitchClass) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    return kNames[((pitchClass % 12) + 12) % 12];
}

/** "D Dorian"; a scale without a name reads "D Custom". */
inline juce::String keyDisplayName(const MusicalScale& key) {
    return keyRootName(key.rootPitchClass) + " " + (key.name.isNotEmpty() ? key.name : juce::String("Custom"));
}

/** The key's notes upward from the root, "D E F G A B C". */
inline juce::String keyNoteNames(const MusicalScale& key) {
    juce::StringArray names;
    for (int i = 0; i < 12; ++i)
        if ((key.mask & (1u << i)) != 0)
            names.add(keyRootName(key.rootPitchClass + i));
    return names.joinIntoString(" ");
}

inline bool sameKey(const MusicalScale& a, const MusicalScale& b) noexcept {
    return a.rootPitchClass == b.rootPitchClass && (a.mask & 0xFFF) == (b.mask & 0xFFF) && a.name == b.name;
}

inline bool sameKey(const std::optional<MusicalScale>& a, const std::optional<MusicalScale>& b) noexcept {
    if (a.has_value() != b.has_value())
        return false;
    return !a.has_value() || sameKey(*a, *b);
}

/** {"root": 0..11, "mask": 1..4095, "name": "..."}. */
inline juce::var keyToVar(const MusicalScale& key) {
    auto* obj = new juce::DynamicObject();
    obj->setProperty("root", ((key.rootPitchClass % 12) + 12) % 12);
    obj->setProperty("mask", (int)(key.mask & 0xFFF));
    obj->setProperty("name", key.name);
    return juce::var(obj);
}

/** nullopt unless `v` is a key keyToVar could have written (root 0..11, a non-empty mask, an optional string name). */
inline std::optional<MusicalScale> keyFromVar(const juce::var& v) {
    auto* obj = v.getDynamicObject();
    if (obj == nullptr)
        return std::nullopt;
    const auto root = obj->getProperty("root");
    const auto mask = obj->getProperty("mask");
    if (!(root.isInt() || root.isInt64() || root.isDouble()) || !(mask.isInt() || mask.isInt64() || mask.isDouble()))
        return std::nullopt;
    const double r = (double)root;
    const double m = (double)mask;
    if (r != std::floor(r) || r < 0.0 || r > 11.0 || m != std::floor(m) || m < 1.0 || m > 4095.0)
        return std::nullopt;
    const auto name = obj->getProperty("name");
    if (!name.isVoid() && !name.isString())
        return std::nullopt;
    return makeScaleFromMask((int)r, (std::uint16_t)m, name.toString());
}

/** Pitch class of a typed note name ("d", "F#", "bb", "Eb"), or -1. */
inline int parseKeyRoot(const juce::String& token) {
    const auto t = token.trim().toLowerCase();
    if (t.isEmpty() || t.length() > 2)
        return -1;
    static const int kNatural[7] = {9, 11, 0, 2, 4, 5, 7}; // a b c d e f g
    const auto letter = t[0];
    if (letter < 'a' || letter > 'g')
        return -1;
    int pc = kNatural[letter - 'a'];
    if (t.length() == 2) {
        if (t[1] == '#')
            ++pc;
        else if (t[1] == 'b')
            --pc;
        else
            return -1;
    }
    return ((pc % 12) + 12) % 12;
}

/** A key picker query split into an optional root (the first word, when it is a note name) and the scale words. */
struct KeyQuery {
    int root = -1;
    juce::String scaleWords;
};

inline KeyQuery parseKeyQuery(const juce::String& query) {
    KeyQuery out;
    auto words = juce::StringArray::fromTokens(query.trim(), " ", "");
    words.removeEmptyStrings();
    if (!words.isEmpty()) {
        out.root = parseKeyRoot(words[0]);
        if (out.root >= 0)
            words.remove(0);
    }
    out.scaleWords = words.joinIntoString(" ");
    return out;
}

/** The built-in preset named `name`, or -1. */
inline int scalePresetIndex(const juce::String& name) {
    const auto& presets = builtInScalePresets();
    for (int i = 0; i < (int)presets.size(); ++i)
        if (name == presets[(size_t)i].name)
            return i;
    return -1;
}

/** The scales a key can be, in picker order and grouped; Chromatic is not a key. */
struct KeyScaleGroup {
    const char* title;
    std::vector<const char*> scales;
};

inline const std::vector<KeyScaleGroup>& keyScaleGroups() {
    static const std::vector<KeyScaleGroup> groups = {
        {"Common", {"Major", "Natural Minor"}},
        {"Modes", {"Dorian", "Phrygian", "Lydian", "Mixolydian", "Locrian"}},
        {"Pentatonic and blues", {"Major Pentatonic", "Minor Pentatonic", "Blues"}},
        {"More", {"Harmonic Minor", "Melodic Minor", "Phrygian Dominant", "Whole Tone"}},
    };
    return groups;
}

/** The key a short text names: a note, then optionally a scale ("E minor", "A dorian", "F# minor", "Bb major",
 *  "e Natural Minor"). A bare note is major; "min"/"minor" and "maj"/"major" are accepted as aliases, and any key
 *  scale name (keyScaleGroups) matches case-insensitively. nullopt when the text is not readable as a key. */
inline std::optional<MusicalScale> parseKeyText(const juce::String& text) {
    auto words = juce::StringArray::fromTokens(text.trim(), " ", "");
    words.removeEmptyStrings();
    if (words.isEmpty())
        return std::nullopt;
    const int root = parseKeyRoot(words[0]);
    if (root < 0)
        return std::nullopt;
    words.remove(0);
    const auto scaleWords = words.joinIntoString(" ").trim().toLowerCase();
    juce::String scaleName = "Major";
    if (scaleWords == "minor" || scaleWords == "min")
        scaleName = "Natural Minor";
    else if (scaleWords.isNotEmpty() && scaleWords != "major" && scaleWords != "maj") {
        scaleName = {};
        for (const auto& group : keyScaleGroups())
            for (const char* name : group.scales)
                if (scaleWords == juce::String(name).toLowerCase())
                    scaleName = name;
    }
    const int preset = scaleName.isEmpty() ? -1 : scalePresetIndex(scaleName);
    if (preset < 0)
        return std::nullopt;
    return makeScale(root, preset);
}

/** Major <-> Natural Minor and Major Pentatonic <-> Minor Pentatonic (same notes, other root); nullopt otherwise. */
inline std::optional<MusicalScale> relativeKey(const MusicalScale& key) {
    struct Pair {
        const char* a;
        const char* b;
    };
    static const Pair kPairs[] = {{"Major", "Natural Minor"}, {"Major Pentatonic", "Minor Pentatonic"}};
    for (const auto& pair : kPairs) {
        if (key.name == pair.a)
            return makeScale(key.rootPitchClass + 9, scalePresetIndex(pair.b));
        if (key.name == pair.b)
            return makeScale(key.rootPitchClass + 3, scalePresetIndex(pair.a));
    }
    return std::nullopt;
}

/** A note range moved to follow a root change: both edges shift by the nearest step from `fromRoot` to `toRoot`
 *  (-5..+6 semitones), keeping its width, then are clamped to 0..127. C2-C3 becomes D2-D3. */
inline std::pair<int, int> rangeFollowingRoot(int lo, int hi, int fromRoot, int toRoot) {
    int delta = (((toRoot - fromRoot) % 12) + 12) % 12;
    if (delta > 6)
        delta -= 12;
    const int width = hi - lo;
    lo = std::clamp(lo + delta, 0, 127 - std::max(0, width));
    return {lo, std::min(127, lo + width)};
}

/** The major or natural minor key that best fits `weights` (how much each pitch class C..B sounds, e.g. summed note
 *  lengths), by correlation with the Krumhansl-Kessler key profiles; nullopt when nothing sounds. Ties go to the
 *  lower root, major before minor. */
inline std::optional<MusicalScale> detectKey(const std::array<double, 12>& weights) {
    static constexpr double kMajor[12] = {6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88};
    static constexpr double kMinor[12] = {6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17};
    double total = 0.0;
    for (double w : weights)
        total += std::max(0.0, w);
    if (total <= 0.0)
        return std::nullopt;

    auto correlation = [&weights](const double* profile, int root) {
        double meanW = 0.0, meanP = 0.0;
        for (int i = 0; i < 12; ++i) {
            meanW += std::max(0.0, weights[(size_t)((root + i) % 12)]);
            meanP += profile[i];
        }
        meanW /= 12.0;
        meanP /= 12.0;
        double num = 0.0, dw = 0.0, dp = 0.0;
        for (int i = 0; i < 12; ++i) {
            const double w = std::max(0.0, weights[(size_t)((root + i) % 12)]) - meanW;
            const double p = profile[i] - meanP;
            num += w * p;
            dw += w * w;
            dp += p * p;
        }
        return dw > 0.0 ? num / std::sqrt(dw * dp) : 0.0;
    };

    double best = -2.0;
    MusicalScale result;
    for (int root = 0; root < 12; ++root) {
        const double major = correlation(kMajor, root);
        if (major > best + 1.0e-12) {
            best = major;
            result = makeScale(root, scalePresetIndex("Major"));
        }
        const double minor = correlation(kMinor, root);
        if (minor > best + 1.0e-12) {
            best = minor;
            result = makeScale(root, scalePresetIndex("Natural Minor"));
        }
    }
    return result;
}

} // namespace synth
