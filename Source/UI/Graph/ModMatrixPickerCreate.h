#pragma once

#include "UI/Graph/ModMatrixPicker.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>
#include <vector>

class AppUndoManager;
class GraphEditor;

namespace synth::ui {

// The "Create new LFO" rows a Mod Matrix row's source picker offers while the typed text names a source type. The
// types come from newModuleSources(), the one list the timeline's and the mod dot's "new source" rows also read.

/** One row per creatable source type, hidden until the query matches that type's name. */
std::vector<ModMatrixPicker::Item> createSourceItems();

/** What a create row makes: its factory type and the output channel the new module is read from. */
struct CreateSourcePick {
    juce::String typeName;
    int channel = 0;
};

/** The type behind a picker row id, or nothing when `id` is not a create row. */
std::optional<CreateSourcePick> createSourcePickFor(int id);

/** Adds the picked module where the user can see it: the middle of the canvas the Mod Matrix panel does not cover (the
 *  nearest free spot there), growing in like any new card, as ONE undo record (`undo` may be null). Returns its node
 * id, invalid when it could not be made. */
juce::AudioProcessorGraph::NodeID createSourceModule(GraphEditor& editor, AppUndoManager* undo,
                                                     const CreateSourcePick& pick);

} // namespace synth::ui
