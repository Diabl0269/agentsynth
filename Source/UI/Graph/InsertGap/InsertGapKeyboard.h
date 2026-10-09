#pragma once

// InsertGapKeyboard.h (docs/layout/layout.md#making-room-for-a-module-dropped-between-others): the keyboard way to
// put a module between two cards. Adding a module from the library without dragging it (Return on a row, or a click)
// while exactly one card is selected puts the new module right after that card in its row: the cards after it slide
// aside exactly as they do for a drag between them, the module joins the card's macro when it is in an open one, and
// the whole add is one undo step.

#include <juce_core/juce_core.h>

class GraphEditor;

namespace synth {

/** Adds library module `name` right after the one selected card. False (nothing added) when the selection is not
 *  exactly one visible card that can have a neighbour (an output-dock card or a macro port widget cannot); the caller
 *  then adds it the usual way. Message thread only. */
bool insertModuleAfterSelectedCard(GraphEditor& editor, const juce::String& name);

} // namespace synth
