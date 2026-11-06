#pragma once

// PitchGrid.h -- the Filter shape window's grid choices as data and the curve grid each one draws: Octaves (1 oct
// down to a semitone, counted from 1 kHz), Notes (every semitone on A 440, named on the C, the notes outside a key
// dotted and never snapped to) and Harmonics (a root and its multiples), plus an independent Level step. Pure: no
// component, so the tests check the line positions against note frequencies directly.
#include "Music/MusicalScale.h"
#include "UI/ModuleViews/CurveEditor/CurveEditorComponent.h"
#include <optional>

namespace synth::ui::pitchgrid {

enum class Kind { off, octaves, notes, harmonics };

/** One window's grid settings, as the rows show them. */
struct Choice {
    Kind kind = Kind::off;
    int stepsPerOctave = 1; ///< Octaves only: 1, 2, 3, 4, 6 or 12
    float levelDb = 0.0f;   ///< 0 is Off (no level lines, levels free); else 12, 6, 3 or 1.5
    bool snapAcross = true;
    bool snapLevel = true;

    bool operator==(const Choice& o) const {
        return kind == o.kind && stepsPerOctave == o.stepsPerOctave && levelDb == o.levelDb &&
               snapAcross == o.snapAcross && snapLevel == o.snapLevel;
    }
};

/** "Off", "Octaves", "Notes", "Harmonics": the kind row, in order. */
const juce::StringArray& kindNames();
/** "1 oct", "1/2", "1/3", "1/4", "1/6", "1/12": the Steps row, in order, with stepCounts() alongside. */
const juce::StringArray& stepNames();
const std::vector<int>& stepCounts();
/** "Off", "12", "6", "3", "1.5": the Level row, with levelSteps() (0 for Off) alongside. */
const juce::StringArray& levelNames();
const std::vector<float>& levelSteps();

/** The grid the old one-row choice ("1 oct", "1/2 oct", "1/3 oct") meant: Octaves at that step with 6 dB levels.
 *  nullopt for any other text. */
std::optional<Choice> fromLegacyName(const juce::String& name);
/** What the choice is called in the window's API and the settings: "Off", "1/3 oct", "Notes", "Harmonics". */
juce::String nameOf(const Choice& choice);

/** MIDI note number to hertz on A4 = 440 Hz, and the note's name ("A4", "C#10"); any octave. */
double noteHz(int midi);
juce::String noteName(int midi);
/** The nearest MIDI note to `hz` (fractional). */
double midiOf(double hz);
/** "440 Hz", "1.26 kHz". */
juce::String formatHz(double hz);
/** "-6 dB", "+1.5 dB", "0 dB" for a curve level. */
juce::String formatDb(float level);

/** What the grid needs besides the choice: the key the Notes grid snaps to (nullopt or chromatic: every note) and the
 *  Harmonics root in hertz. */
struct Context {
    std::optional<MusicalScale> key;
    double rootHz = 110.0;
};

/** The curve grid for a choice; nullopt when it is free on both axes (kind Off and Level Off). */
std::optional<CurveEditorComponent::CurveGrid> gridFor(const Choice& choice, const Context& context);

} // namespace synth::ui::pitchgrid
