#include "StepGridChords.h"
#include "Music/NoteNames.h"
#include "StepGridModel.h"
#include <algorithm>

namespace synth::ui::stepgrid {

namespace {

ScaleChord scaleChordOf(SequencerChord chord, int root, const MusicalScale& scale, std::uint32_t seed, int stepIndex) {
    const auto notes = sequencerChordNotes(chord, root, scale, seed, stepIndex);
    ScaleChord out;
    out.rootPitch = root;
    for (int i = 0; i < notes.count && out.count < (int)out.notes.size(); ++i)
        out.notes[(size_t)out.count++] = notes.notes[(size_t)i];
    return out;
}

std::vector<int> sortedNotes(const ScaleChord& chord) {
    std::vector<int> notes(chord.notes.begin(), chord.notes.begin() + chord.count);
    std::sort(notes.begin(), notes.end());
    return notes;
}

juce::String pitchClassName(int pitch) { return noteNameWithOctave(pitch).trimCharactersAtEnd("0123456789-"); }

// The chord types that are not a plain stack of named intervals: the label spells them with the note and the type.
bool isPlainNote(SequencerChord chord) {
    return chord == SequencerChord::Unison || chord == SequencerChord::Fifths || chord == SequencerChord::Octaves ||
           chord == SequencerChord::Random;
}

} // namespace

ChordLabel chordLabelFor(SequencerChord chord, int playedRoot, const MusicalScale& scale, std::uint32_t seed,
                         int stepIndex) {
    if (chord == SequencerChord::Unison)
        return {noteName(playedRoot), {}};
    if (isPlainNote(chord))
        return {noteName(playedRoot) + " " + chordId(chord), {}};
    const auto built = scaleChordOf(chord, playedRoot, scale, seed, stepIndex);
    ChordLabel label;
    label.name = chordName(built);
    if (!scale.isChromatic() && scale.mask != 0)
        label.numeral = romanNumeral(scale, built);
    return label;
}

ChordPickerContent chordPickerContent(int rootPitch, const MusicalScale& scale) {
    ChordPickerContent content;
    const juce::String rootName = pitchClassName(rootPitch);
    const bool hasScale = !scale.isChromatic() && scale.mask != 0;
    content.header = hasScale ? "Chords that fit " + pitchClassName(scale.rootPitchClass) + " " +
                                    (scale.name.isNotEmpty() ? scale.name.toLowerCase() : juce::String("scale")) +
                                    " on " + rootName
                              : "Chords on " + rootName;

    const auto inKey = [&](SequencerChord chord) {
        return ChordChip{chord, chordName(scaleChordOf(chord, rootPitch, scale, 0, 0))};
    };
    content.rows.push_back({"Triad", {inKey(SequencerChord::ScaleTriad)}});
    content.rows.push_back({"Seventh", {inKey(SequencerChord::ScaleSeventh)}});
    content.rows.push_back({"Sus", {inKey(SequencerChord::ScaleSus2), inKey(SequencerChord::ScaleSus4)}});
    content.rows.push_back({"Add", {inKey(SequencerChord::ScaleAdd9), inKey(SequencerChord::ScaleSixth)}});
    content.firstOutsideRow = (int)content.rows.size();

    std::vector<std::vector<int>> inKeyNotes;
    for (const auto& row : content.rows)
        for (const auto& chip : row.chips)
            inKeyNotes.push_back(sortedNotes(scaleChordOf(chip.chord, rootPitch, scale, 0, 0)));

    ChordRow borrowed{"Borrowed", {}};
    for (const auto chord : {SequencerChord::Major, SequencerChord::Minor, SequencerChord::Dom7, SequencerChord::Min7,
                             SequencerChord::Maj7, SequencerChord::Dim, SequencerChord::Aug, SequencerChord::Sus2,
                             SequencerChord::Sus4, SequencerChord::Sixth, SequencerChord::Min6, SequencerChord::Add9,
                             SequencerChord::Dim7, SequencerChord::HalfDim7, SequencerChord::MinMaj7}) {
        const auto built = scaleChordOf(chord, rootPitch, scale, 0, 0);
        if (std::find(inKeyNotes.begin(), inKeyNotes.end(), sortedNotes(built)) != inKeyNotes.end())
            continue;
        borrowed.chips.push_back({chord, chordName(built)});
    }
    content.rows.push_back(std::move(borrowed));

    ChordRow more{"More", {}};
    for (const auto chord :
         {SequencerChord::Unison, SequencerChord::Fifths, SequencerChord::Octaves, SequencerChord::Random})
        more.chips.push_back({chord, chordId(chord)});
    content.rows.push_back(std::move(more));
    return content;
}

} // namespace synth::ui::stepgrid
