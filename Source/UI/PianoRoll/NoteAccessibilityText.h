#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>

// NoteAccessibilityText.h: what a screen reader says for the piano roll's focused note, e.g.
// "C4, bar 2 beat 1, length 1/8, velocity 100", and for the roll as a whole when no note is
// focused. Pure, so the wording is unit-testable without a component or a native accessibility peer.

namespace synth::ui {

/** The note's name with its octave, middle C (pitch 60) being "C4" and sharps written "C#4". */
inline juce::String describeNotePitch(int pitch) {
    static const char* const kNames[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int pitchClass = ((pitch % 12) + 12) % 12;
    const int octave = (int)std::floor((double)pitch / 12.0) - 1;
    return juce::String(kNames[pitchClass]) + juce::String(octave);
}

/** A 1-based "bar 2 beat 1" for `beat` (timeline beats from 0); the beat carries a fraction
 *  ("beat 2.5") when the note starts between beats. */
inline juce::String describeNotePosition(double beat, double beatsPerBar) {
    const double bpb = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const double clamped = std::max(0.0, beat);
    const int bar = (int)std::floor(clamped / bpb + 1e-9);
    const double beatInBar = std::max(0.0, clamped - (double)bar * bpb) + 1.0;
    return "bar " + juce::String(bar + 1) + " beat " +
           juce::String(beatInBar, 2).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
}

/** A note length as a fraction of a whole note when it is exact ("1/8", "3/16", "1/1"), otherwise
 *  in beats ("0.3 beats"). A beat is a quarter note. */
inline juce::String describeNoteLength(double lengthBeats) {
    const double whole = std::max(0.0, lengthBeats) / 4.0;
    for (const int denominator : {1, 2, 4, 8, 16, 32, 64}) {
        const double scaled = whole * (double)denominator;
        const double numerator = std::round(scaled);
        if (numerator >= 1.0 && std::abs(scaled - numerator) < 1e-6) {
            auto n = (int)numerator;
            auto d = denominator;
            while (n % 2 == 0 && d % 2 == 0) {
                n /= 2;
                d /= 2;
            }
            return juce::String(n) + "/" + juce::String(d);
        }
    }
    const auto beats = juce::String(lengthBeats, 3).trimCharactersAtEnd("0").trimCharactersAtEnd(".");
    return beats + (beats == "1" ? " beat" : " beats");
}

/** The focused note: pitch, start (`startBeat` is on the timeline, not clip-relative), length and
 *  velocity. */
inline juce::String describeNoteForAccessibility(const synth::MidiNote& note, double startBeat, double beatsPerBar) {
    return describeNotePitch(note.pitch) + ", " + describeNotePosition(startBeat, beatsPerBar) + ", length " +
           describeNoteLength(note.lengthBeats) + ", velocity " + juce::String(note.velocity);
}

/** The roll with no single note focused: the clip's name and note count, plus how many notes are
 *  selected when more than one is. */
inline juce::String describeRollForAccessibility(const juce::String& clipName, int noteCount, int selectedCount) {
    juce::String text =
        clipName + ", " + (noteCount == 1 ? juce::String("1 note") : juce::String(noteCount) + " notes");
    if (selectedCount > 1)
        text += ", " + juce::String(selectedCount) + " selected";
    return text;
}

} // namespace synth::ui
