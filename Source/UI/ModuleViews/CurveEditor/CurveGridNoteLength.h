#pragma once

// CurveGridNoteLength.h -- the note-length grid of a time-axis curve editor: the names its box offers and what each
// one means for the editor's grid and snapping. The LFO card's custom wave and the LFO shape window share it.
#include "CurveEditorComponent.h"
#include "UI/Layout/NoteLengthMenu/NoteLengthBox.h"
#include <juce_core/juce_core.h>
#include <optional>

namespace synth::ui::curvegrid {

/** "Off", then the straight steps 1/4 .. 1/128, then the triplets 1/4T .. 1/128T. A dotted step does not divide the
 *  cycle evenly, so the list has none. */
juce::StringArray noteLengthNames();
/** The step the LFO card starts on. */
juce::String defaultNoteLengthName();
/** Divisions per axis a name stands for: N for "1/N", N * 3 / 2 for "1/NT" (three steps in the room of two straight
 *  ones), 0 for "Off". */
int noteLengthDivisions(const juce::String& name);
/** The grid for `divisions` (more than 0): that many cells across, as many up (at most 8). */
CurveEditorComponent::CurveGrid gridForDivisions(int divisions);
/** Fills `box` with the names (written "Grid 1/8" when closed) and explains the missing dotted kind. */
void setUpNoteLengthBox(NoteLengthBox& box);
/** Sets `editor`'s grid and snapping for `divisions`: snap to `gridForDivisions` when more than 0, else no snapping
 *  and `offGrid` as the grid (nullopt draws the editor's own time grid). */
void applyDivisions(CurveEditorComponent& editor, int divisions,
                    std::optional<CurveEditorComponent::CurveGrid> offGrid);

} // namespace synth::ui::curvegrid
