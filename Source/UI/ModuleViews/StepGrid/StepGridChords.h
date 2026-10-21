#pragma once

#include "Modules/Sequencer/SequencerChords.h"
#include "Modules/Sequencer/SequencerPattern.h"
#include "Music/MusicalScale.h"
#include <juce_core/juce_core.h>
#include <vector>

// The Poly Sequencer's chord naming and the chord picker's contents, pure so they are unit-tested directly. A chord
// is stored as a type; its notes come from the step's played pitch.
// docs/modules/sequencer-design.md#poly-sequencer-chords.
namespace synth::ui::stepgrid {

struct ChordChip {
    SequencerChord chord = SequencerChord::Unison;
    juce::String name;
};

struct ChordRow {
    juce::String label;
    std::vector<ChordChip> chips;
};

/** What the picker shows for a step whose played pitch is `rootPitch`: the in-key rows, then the rows outside the key.
 *  `firstOutsideRow` is the index of the first row under "Outside the key". */
struct ChordPickerContent {
    juce::String header;
    std::vector<ChordRow> rows;
    int firstOutsideRow = 0;
};

ChordPickerContent chordPickerContent(int rootPitch, const MusicalScale& scale);

/** A step's label over its bar: the chord's name, and with a scale its roman numeral. A single note is just the note
 *  name (octave numbering where middle C is C3). */
struct ChordLabel {
    juce::String name;
    juce::String numeral;
    juce::String text() const { return numeral.isEmpty() ? name : name + " " + numeral; }
    bool operator==(const ChordLabel&) const = default;
};

ChordLabel chordLabelFor(SequencerChord chord, int playedRoot, const MusicalScale& scale, std::uint32_t seed,
                         int stepIndex);

} // namespace synth::ui::stepgrid
