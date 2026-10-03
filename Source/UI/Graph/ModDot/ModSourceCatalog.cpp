#include "ModSourceCatalog.h"

#include "AudioEngine/ModuleTitle.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include <map>

namespace synth::ui {

namespace {
ModSourceGroup groupFor(const ModuleBase& module) {
    if (dynamic_cast<const MacroControlModule*>(&module) != nullptr)
        return ModSourceGroup::Macros;
    if (dynamic_cast<const ExternalMidiModule*>(&module) != nullptr ||
        dynamic_cast<const MidiKeyboardModule*>(&module) != nullptr ||
        dynamic_cast<const PolyMidiModule*>(&module) != nullptr ||
        dynamic_cast<const TimelineMidiSourceModule*>(&module) != nullptr)
        return ModSourceGroup::Midi;
    switch (module.getModulationCategory()) {
    case ModulationCategory::LFO:
        return ModSourceGroup::Lfos;
    case ModulationCategory::Envelope:
        return ModSourceGroup::Envelopes;
    case ModulationCategory::Sequencer:
        return ModSourceGroup::Sequencers;
    case ModulationCategory::Oscillator:
        return ModSourceGroup::Oscillators;
    case ModulationCategory::Filter:
        return ModSourceGroup::Filters;
    case ModulationCategory::FX:
        return ModSourceGroup::Effects;
    case ModulationCategory::Other:
        break;
    }
    return ModSourceGroup::Other;
}
} // namespace

std::vector<ModSourceItem> enumerateModSources(juce::AudioProcessorGraph& graph) {
    std::map<ModulationCategory, std::vector<juce::AudioProcessorGraph::Node*>> byCategory;
    for (auto* node : graph.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            byCategory[module->getModulationCategory()].push_back(node);

    std::vector<ModSourceItem> out;
    for (const auto& [category, nodes] : byCategory) {
        for (auto* node : nodes) {
            const auto& module = *static_cast<ModuleBase*>(node->getProcessor());
            for (const auto& output : modSourceOutputs(module)) {
                ModSourceItem item;
                item.node = node->nodeID;
                item.channel = output.channel;
                item.moduleTitle = synth::moduleTitle(*node);
                item.outputLabel = output.label;
                item.category = category;
                item.group = groupFor(module);
                out.push_back(std::move(item));
            }
        }
    }
    return out;
}

juce::String modSourceGroupName(ModSourceGroup group) {
    switch (group) {
    case ModSourceGroup::Lfos:
        return "LFOs";
    case ModSourceGroup::Envelopes:
        return "Envelopes";
    case ModSourceGroup::Macros:
        return "Macros";
    case ModSourceGroup::Midi:
        return "MIDI";
    case ModSourceGroup::Sequencers:
        return "Sequencers";
    case ModSourceGroup::Oscillators:
        return "Oscillators";
    case ModSourceGroup::Filters:
        return "Filters";
    case ModSourceGroup::Effects:
        return "Effects";
    case ModSourceGroup::Other:
        break;
    }
    return "Other";
}

} // namespace synth::ui
