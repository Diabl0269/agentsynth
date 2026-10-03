#pragma once

// Every modulation source the graph offers, one entry per module output: the single list the Mod Matrix's
// source combos and the mod dot's "Add source" page both read, so the two can never disagree about what can
// drive a parameter. docs/modules/modulation.md#the-mod-dot-menu.

#include "Modules/ModuleBase.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

namespace synth::ui {

/** How the Add source page groups a source. The Mod Matrix keeps its own ModulationCategory grouping. */
enum class ModSourceGroup { Lfos, Envelopes, Macros, Midi, Sequencers, Oscillators, Filters, Effects, Other };

struct ModSourceItem {
    juce::AudioProcessorGraph::NodeID node;
    int channel = 0;          // the raw output channel a routing reads
    juce::String moduleTitle; // the card's title (a rename shows)
    juce::String outputLabel; // the jack's name; empty when the module offers a single entry
    ModulationCategory category = ModulationCategory::Other;
    ModSourceGroup group = ModSourceGroup::Other;

    /** The Mod Matrix's combo id for this source: node id and channel packed together. */
    int itemId() const noexcept { return (int)((node.uid << 8) | (juce::uint32)channel); }
    /** "LFO 1", or "Envelope 1 - Env" for a module with several outputs. */
    juce::String label() const { return outputLabel.isEmpty() ? moduleTitle : moduleTitle + " - " + outputLabel; }
};

/** Every source in `graph`, in the Mod Matrix's order: by ModulationCategory, then graph order, then output. */
std::vector<ModSourceItem> enumerateModSources(juce::AudioProcessorGraph& graph);

/** The Add source page's heading for `group` ("LFOs", "Macros", ...). */
juce::String modSourceGroupName(ModSourceGroup group);

} // namespace synth::ui
