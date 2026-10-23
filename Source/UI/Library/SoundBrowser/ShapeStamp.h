#pragma once

#include "Modules/Shapes/ShapeLibrary.h"
#include <juce_core/juce_core.h>

class GraphEditor;

namespace synth::ui {

/** Return on a Shape row: stamps `shape` onto the shape nearest the morph cursor of the one selected card, when that
 *  card is a Filter on its Custom type or an LFO on a Custom wave and the kinds match; one undo step. Returns the
 * card's name ("Filter", "LFO") when it landed, and an empty string, changing nothing, when no card fits. */
juce::String stampShapeOnSelectedCard(GraphEditor& editor, const synth::shapes::LibraryShape& shape);

} // namespace synth::ui
