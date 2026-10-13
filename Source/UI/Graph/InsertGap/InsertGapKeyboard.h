#pragma once

// InsertGapKeyboard.h (docs/layout/layout.md#making-room-for-a-module-dropped-between-others): the keyboard way to
// put a module between two cards. Adding a module from the library without dragging it (Return on a row, or a click)
// while exactly one card is selected puts the new module right after that card in its row: the cards after it slide
// aside exactly as they do for a drag between them, the module joins the card's macro when it is in an open one, and
// the whole add is one undo step.

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

class AppUndoManager;
class GraphEditor;

namespace synth {

/** Adds library module `name` right after the one selected card. False (nothing added) when the selection is not
 *  exactly one visible card that can have a neighbour (an output-dock card or a macro port widget cannot); the caller
 *  then adds it the usual way. Message thread only. */
bool insertModuleAfterSelectedCard(GraphEditor& editor, const juce::String& name);

class MacroSet;

/** The mixer's add (docs/mixer/mixer.md#adding-an-insert): makes library module `name`, puts its card right after
 *  `anchor`'s card as insertModuleAfterSelectedCard does (the cards after it slide aside, the new card grows in), and
 *  runs `wire` on the new node to cable it in, all as ONE graph-and-macro undo step. An anchor with no card on screen
 *  (inside a collapsed macro) places the node one spacing to its right without moving anything. Returns the new node,
 *  or an invalid id when nothing was added (`wire` refused, or the module could not be made). Message thread only. */
juce::AudioProcessorGraph::NodeID
insertModuleAfterNode(GraphEditor& editor, AppUndoManager& undoManager, MacroSet& macros, const juce::String& name,
                      juce::AudioProcessorGraph::NodeID anchor,
                      const std::function<bool(juce::AudioProcessorGraph::NodeID)>& wire);

} // namespace synth
