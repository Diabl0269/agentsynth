#pragma once

// Pure musical-scale logic for the piano roll's scale-assist feature: scale membership,
// nearest-pitch snapping, built-in scale presets, user-scale persistence, and random note
// generation constrained to a scale. Deliberately header-only and free of any UI or
// TimelineDoc-mutation dependency — it only needs MidiNote's field layout to build notes, never
// TimelineDoc's mutation API, so callers stay responsible for how generated notes get inserted
// (through the normal mutation API + undo, same as any other note edit).

#include "TimelineDoc.h"
#include <algorithm>
#include <cstdint>
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
};

// Root-relative interval masks for the built-in scale library. Bit 0 (root itself) is always
// set for every real scale; only the degenerate/unused mask 0 has it clear.
struct ScalePreset {
    const char* name;
    std::uint16_t mask;
};

inline const std::vector<ScalePreset>& builtInScalePresets() {
    static const std::vector<ScalePreset> presets = {
        {"Major", 0x0AB5},            // 0,2,4,5,7,9,11
        {"Natural Minor", 0x05AD},    // 0,2,3,5,7,8,10
        {"Harmonic Minor", 0x09AD},   // 0,2,3,5,7,8,11
        {"Melodic Minor", 0x0AAD},    // 0,2,3,5,7,9,11
        {"Dorian", 0x06AD},           // 0,2,3,5,7,9,10
        {"Phrygian", 0x05AB},         // 0,1,3,5,7,8,10
        {"Lydian", 0x0AD5},           // 0,2,4,6,7,9,11
        {"Mixolydian", 0x06B5},       // 0,2,4,5,7,9,10
        {"Locrian", 0x056B},          // 0,1,3,5,6,8,10
        {"Major Pentatonic", 0x0295}, // 0,2,4,7,9
        {"Minor Pentatonic", 0x04A9}, // 0,3,5,7,10
        {"Blues", 0x04E9},            // 0,3,5,6,7,10
        {"Whole Tone", 0x0555},       // 0,2,4,6,8,10
        {"Chromatic", 0x0FFF},        // all 12
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

// Generates one note per grid step, starting at beat 0, for as long as the step's start still
// falls (with a small epsilon) before clipLengthBeats. Pitch is chosen uniformly among the
// in-scale pitches inside [minPitch, maxPitch] (every pitch in range when scale is null or
// chromatic); if no candidate pitch exists in range at all, returns empty rather than falling
// back to an out-of-range or out-of-scale note. RNG is caller-owned and caller-seeded so this
// function is deterministic and independently testable.
inline std::vector<synth::MidiNote> generateRandomNotes(double clipLengthBeats, double gridBeats, int minPitch,
                                                        int maxPitch, const MusicalScale* scale, juce::Random& rng) {
    std::vector<synth::MidiNote> notes;
    if (gridBeats <= 0.0 || clipLengthBeats <= 0.0)
        return notes;

    int lo = std::min(minPitch, maxPitch);
    int hi = std::max(minPitch, maxPitch);
    lo = std::clamp(lo, 0, 127);
    hi = std::clamp(hi, 0, 127);

    std::vector<int> candidates;
    for (int pitch = lo; pitch <= hi; ++pitch) {
        if (scale == nullptr || scale->isChromatic() || scale->contains(pitch))
            candidates.push_back(pitch);
    }
    if (candidates.empty())
        return notes;

    constexpr double eps = 1e-9;
    for (double start = 0.0; start + eps < clipLengthBeats; start += gridBeats) {
        double remaining = clipLengthBeats - start;
        double length = std::min(gridBeats, remaining);

        synth::MidiNote note;
        note.startBeat = start;
        note.lengthBeats = length;
        int pickIndex = rng.nextInt((int)candidates.size());
        note.pitch = candidates[(size_t)pickIndex];
        note.velocity = 100;
        note.channel = 1;
        note.muted = false;
        notes.push_back(note);
    }
    return notes;
}

} // namespace synth
