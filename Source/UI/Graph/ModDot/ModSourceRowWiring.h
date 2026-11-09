#pragma once

// The one wiring of a modulation row's controls to the graph, shared by every surface that lists a modulation (the
// knob's sources page and the port connections panel's mod rows), so the two cannot drift: the curve chip, the amount
// drag and typed amount (one undo step each) and "show in timeline". Selection, removal and the keys stay with each
// surface. docs/modules/modulation.md#the-mod-dot-menu.

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

class GraphEditor;

namespace synth::ui {

class ModDotSourceRow;

/** Wires `row` (its attenuverter is the chain edited) to `editor`; `card` and `destChannel` name the knob the chain
 *  lands on, which "show in timeline" reveals. `afterTypedAmount` runs once a typed value has been applied. */
void wireModSourceRow(ModDotSourceRow& row, GraphEditor& editor, juce::AudioProcessorGraph::NodeID card,
                      int destChannel, std::function<void()> afterTypedAmount);

/** Moves attenuverter `attenuverterId`'s amount to `amount`. Call between begin/commitModAmountGesture for one undo
 *  step. */
void applyModAmount(GraphEditor& editor, juce::AudioProcessorGraph::NodeID attenuverterId, float amount);

} // namespace synth::ui
