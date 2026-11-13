#pragma once

// Every modulation source the graph offers, one entry per module output: the single list the Mod Matrix's
// source combos and the mod dot's "Add source" page both read, so the two can never disagree about what can
// drive a parameter. docs/modules/modulation.md#the-mod-dot-menu.

#include "Modules/ModuleBase.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

class GraphEditor;

namespace synth::ui {

/** How the Add source list groups a source. The Mod Matrix keeps its own ModulationCategory grouping. */
enum class ModSourceGroup {
    Lfos,
    Envelopes,
    Macros,
    Midi,
    Sequencers,
    Oscillators,
    Filters,
    Effects,
    Other,
    NewModule // the "New <module>" rows a search offers
};

struct ModSourceItem {
    juce::AudioProcessorGraph::NodeID node;
    int channel = 0;          // the raw output channel a routing reads
    juce::String moduleTitle; // the card's title (a rename shows)
    juce::String outputLabel; // the jack's name; empty when the module offers a single entry
    ModulationCategory category = ModulationCategory::Other;
    ModSourceGroup group = ModSourceGroup::Other;
    juce::String aliases; // other names the module goes by ("env" for an ADSR); searched, never shown

    /** The Mod Matrix's combo id for this source: node id and channel packed together. */
    int itemId() const noexcept { return (int)((node.uid << 8) | (juce::uint32)channel); }
    /** "LFO 1", or "Envelope 1 - Env" for a module with several outputs. */
    juce::String label() const { return outputLabel.isEmpty() ? moduleTitle : moduleTitle + " - " + outputLabel; }
    /** What a search matches: the label plus the aliases. */
    juce::String searchText() const { return aliases.isEmpty() ? label() : label() + " " + aliases; }
};

/** Every source in `graph`, in the Mod Matrix's order: by ModulationCategory, then graph order, then output. */
std::vector<ModSourceItem> enumerateModSources(juce::AudioProcessorGraph& graph);

/** A module type the search can create as a new source ("New Env"): its factory key, the name its row shows after
 *  "New ", the extra words a search matches, and the output channel the new source is read from (its first
 *  modulation output). */
struct NewModuleSource {
    juce::String typeName;
    juce::String label;
    juce::String aliases;
    int channel = 0;
};

/** The factory types worth offering as a new source: the authorable modulators (LFOs, envelopes, macros, sequencers,
 *  oscillators) that are not singletons. Probed from the factory once, in factory-key order. Keys that make the same
 *  module class (the ADSR, Amp Env and Filter Env keys are one class) are one entry under the first key, so the
 *  list offers a single "New Env"; the other keys stay loadable and searchable through `aliases`. */
const std::vector<NewModuleSource>& newModuleSources();

/** How many things each source moves: the modulation routings leaving it (macro ports looked through), counting each
 *  distinct destination jack once, keyed by ModSourceItem::itemId(). `fresh` asks the engine instead of the editor's
 *  cached routings. Message thread only. */
std::map<int, int> modSourceTargetCounts(GraphEditor& editor, bool fresh);

/** "Not used yet", "1 target" or "N targets". */
juce::String modSourceUsageText(int targets);

/** The Add source list's heading for `group` ("LFOs", "Macros", ...). */
juce::String modSourceGroupName(ModSourceGroup group);

} // namespace synth::ui
