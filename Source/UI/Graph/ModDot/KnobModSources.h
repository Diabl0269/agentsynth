#pragma once

// The modulation sources landing on one knob or fader, looking through macro ports, so a card's mod
// dot (its tooltip, its keyboard stop) and its hover chip all name what the user patched rather than
// the port the cable happens to enter by. docs/modules/modulation.md#drag-to-knob-modulation.

#include "AudioEngine/ModulationRoutingTypes.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <utility>
#include <vector>

class GraphEditor;

namespace synth::ui {

/** One attenuverter routing onto a knob. */
struct KnobModSource {
    juce::AudioProcessorGraph::NodeID attenuverterId;
    juce::AudioProcessorGraph::NodeID sourceNodeId; // the real modulator, behind any macro ports
    int sourceChannel = 0;                          // the raw output channel of it the routing reads
    juce::String sourceName;                        // its title (a rename shows), empty if the node is gone
    juce::String fullName; // the unshortened name for a tooltip / screen reader; empty = sourceName
    float amount = 1.0f;   // the attenuverter's live "amount", -1..1
    bool bypassed = false;

    /** What a tooltip and a screen reader say: `fullName` when the row shows a short form, else `sourceName`. */
    const juce::String& spokenName() const noexcept { return fullName.isNotEmpty() ? fullName : sourceName; }
};

/** One AttenuverterChain routing and the knob it lands on, macro ports looked through. */
struct KnobModChain {
    juce::AudioProcessorGraph::NodeID card; // the module whose knob (or CV jack) the chain drives
    int destChannel = -1;                   // its raw CV channel
    KnobModSource source;
};

/** Every AttenuverterChain routing, as knobModSources words each (same order, same `fresh` rule). */
std::vector<KnobModChain> knobModChains(GraphEditor& editor, bool fresh = false);

/** Every AttenuverterChain routing onto (`dest`, `destChannel`), in GraphEditor::getCachedModDisplayInfo()
 *  order. `fresh` reads the engine's routings now instead of the editor's cache (which only the editor's 30 Hz tick
 *  refreshes): for a caller that just edited the graph. Message thread only. */
std::vector<KnobModSource> knobModSources(GraphEditor& editor, juce::AudioProcessorGraph::NodeID dest, int destChannel,
                                          bool fresh = false);

/** How many AttenuverterChain routings land on each (destination node uid, channel), looking through macro ports
 *  like knobModSources: every routing resolved once against one cable index, so the cost grows with the routing
 *  count, never with routings x cards. */
std::map<std::pair<juce::uint32, int>, int> countKnobModSources(GraphEditor& editor,
                                                                const std::vector<ModulationRouting>& routings);

/** The knob a destination channel drives, as the mod dot's menu words it. */
struct KnobModTarget {
    juce::String paramId;   // the bound parameter's id (what a timeline lane is keyed by)
    juce::String paramName; // its display name ("Cutoff"); the jack's name when it binds to no parameter
    bool valid() const { return paramName.isNotEmpty(); }
};
KnobModTarget knobModTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card, int destChannel);

/** The attenuverter's live "amount" (-1..1), or `fallback` when the node or parameter is gone. */
float attenuverterAmount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID attenuverterId,
                         float fallback = 0.0f);

} // namespace synth::ui
