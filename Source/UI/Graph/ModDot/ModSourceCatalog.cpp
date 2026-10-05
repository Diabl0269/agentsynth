#include "ModSourceCatalog.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/ExternalMidiModule.h"
#include "Modules/MacroControlModule.h"
#include "Modules/MidiKeyboardModule.h"
#include "Modules/PolyMidiModule.h"
#include "Modules/TimelineMidiSourceModule.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include <map>
#include <set>

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
    case ModSourceGroup::NewModule:
        return "New module";
    case ModSourceGroup::Other:
        break;
    }
    return "Other";
}

const std::vector<NewModuleSource>& newModuleSources() {
    // Probed rather than listed, like AIStateMapper::dualIOCapableModuleTypes: one throwaway instance per authorable
    // type, so a new modulator module shows up in the search without an edit here.
    static const std::vector<NewModuleSource> types = [] {
        std::vector<NewModuleSource> out;
        for (const auto& name : synth::AIStateMapper::authorableModuleTypes()) {
            if (GraphEditor::isSingletonIOModule(name))
                continue;
            auto probe = synth::AIStateMapper::createModule(name);
            auto* module = dynamic_cast<ModuleBase*>(probe.get());
            if (module == nullptr)
                continue;
            const auto group = groupFor(*module);
            if (group != ModSourceGroup::Lfos && group != ModSourceGroup::Envelopes &&
                group != ModSourceGroup::Macros && group != ModSourceGroup::Sequencers &&
                group != ModSourceGroup::Oscillators)
                continue;
            const auto outputs = modSourceOutputs(*module);
            if (!outputs.empty())
                out.push_back({name, outputs.front().channel});
        }
        return out;
    }();
    return types;
}

std::map<int, int> modSourceTargetCounts(GraphEditor& editor, bool fresh) {
    auto& graph = editor.getAudioEngine().getGraph();
    const auto isPort = [&editor](juce::AudioProcessorGraph::NodeID id) {
        return editor.getMacroController().nodeIsMacroPort(id);
    };
    const auto freshRoutings =
        fresh ? editor.getAudioEngine().getModulationRoutings() : std::vector<ModulationRouting>();
    const auto& routings = fresh ? freshRoutings : editor.getCachedModRoutings();
    std::map<int, std::set<std::pair<juce::uint32, int>>> destinations;
    for (const auto& routing : routings) {
        if (!routing.hasSource || !routing.hasDest)
            continue;
        const auto real = resolveRouting(graph, routing, isPort);
        if (!real.source.valid() || !real.dest.valid())
            continue;
        ModSourceItem item;
        item.node = real.source.node;
        item.channel = real.source.channel;
        destinations[item.itemId()].insert({real.dest.node.uid, real.dest.channel});
    }
    std::map<int, int> counts;
    for (const auto& [id, dests] : destinations)
        counts[id] = (int)dests.size();
    return counts;
}

juce::String modSourceUsageText(int targets) {
    if (targets <= 0)
        return "Not used yet";
    return targets == 1 ? juce::String("1 target") : juce::String(targets) + " targets";
}

} // namespace synth::ui
