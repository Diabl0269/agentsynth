// CurveGridNoteLength.cpp -- see CurveGridNoteLength.h.
#include "CurveGridNoteLength.h"
#include "Modules/Envelope/NoteLength.h"

namespace synth::ui::curvegrid {

namespace {
const char* const kStraightNames[] = {"1/4", "1/8", "1/16", "1/32", "1/64", "1/128"};
constexpr int kMaxHorizontalCells = 8;
} // namespace

juce::StringArray noteLengthNames() {
    juce::StringArray names{"Off"};
    for (const char* name : kStraightNames)
        names.add(name);
    for (const char* name : kStraightNames)
        names.add(juce::String(name) + "T");
    return names;
}

juce::String defaultNoteLengthName() { return "1/8"; }

int noteLengthDivisions(const juce::String& name) {
    const auto note = synth::parseNoteLength(name);
    if (!note.has_value())
        return 0; // "Off"
    return note->kind == synth::NoteKind::triplet ? note->denominator * 3 / 2 : note->denominator;
}

CurveEditorComponent::CurveGrid gridForDivisions(int divisions) {
    return CurveEditorComponent::CurveGrid{divisions, juce::jmin(divisions, kMaxHorizontalCells)};
}

void setUpNoteLengthBox(NoteLengthBox& box) {
    box.setChoices(noteLengthNames(), "Grid ");
    box.setKindUnavailableTip(synth::NoteKind::dotted,
                              "No dotted grid: a dotted step does not divide the cycle evenly");
}

void applyDivisions(CurveEditorComponent& editor, int divisions,
                    std::optional<CurveEditorComponent::CurveGrid> offGrid) {
    if (divisions <= 0) {
        editor.setGrid(offGrid);
        editor.setSnapToGrid(false);
        return;
    }
    editor.setGrid(gridForDivisions(divisions));
    editor.setSnapToGrid(true);
}

} // namespace synth::ui::curvegrid
