#pragma once

// The modulation sources landing on one knob or fader, looking through macro ports, so a card's mod
// dot (its tooltip, its keyboard stop) and its hover chip all name what the user patched rather than
// the port the cable happens to enter by. docs/modules/modulation.md#drag-to-knob-modulation.

#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

class GraphEditor;

namespace synth::ui {

/** One attenuverter routing onto a knob. */
struct KnobModSource {
    juce::AudioProcessorGraph::NodeID attenuverterId;
    juce::AudioProcessorGraph::NodeID sourceNodeId; // the real modulator, behind any macro ports
    juce::String sourceName;                        // its title (a rename shows), empty if the node is gone
    float amount = 1.0f;                            // the attenuverter's live "amount", -1..1
    bool bypassed = false;
};

/** Every AttenuverterChain routing onto (`dest`, `destChannel`), in GraphEditor::getCachedModDisplayInfo()
 *  order. Message thread only. */
std::vector<KnobModSource> knobModSources(GraphEditor& editor, juce::AudioProcessorGraph::NodeID dest, int destChannel);

/** The attenuverter's live "amount" (-1..1), or `fallback` when the node or parameter is gone. */
float attenuverterAmount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID attenuverterId,
                         float fallback = 0.0f);

} // namespace synth::ui
