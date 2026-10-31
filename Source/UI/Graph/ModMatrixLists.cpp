#include "UI/Graph/ModMatrixLists.h"

#include "AudioEngine/ModuleTitle.h"
#include "Modules/ModuleBase.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Graph/ModDot/ModSourceCatalog.h"
#include "UI/Graph/ModMatrixEndpoints.h"
#include <map>

namespace synth::ui {

namespace {
const std::map<ModulationCategory, juce::String>& modulationCategoryNames() {
    static const std::map<ModulationCategory, juce::String> names = {{ModulationCategory::Envelope, "Envelopes"},
                                                                     {ModulationCategory::LFO, "LFOs"},
                                                                     {ModulationCategory::Oscillator, "Oscillators"},
                                                                     {ModulationCategory::Sequencer, "Sequencers"},
                                                                     {ModulationCategory::Filter, "Filters"},
                                                                     {ModulationCategory::FX, "Effects"},
                                                                     {ModulationCategory::Other, "Other"}};
    return names;
}
} // namespace

void ModMatrixLists::invalidate() {
    sources_ = {};
    destinations_ = {};
}

void ModMatrixLists::setFlat(bool flat) {
    if (flat_ != flat) {
        flat_ = flat;
        invalidate();
    }
}

const std::vector<ModMatrixPicker::Item>& ModMatrixLists::sources() {
    if (!sources_.built)
        buildSources();
    return sources_.items;
}

const std::vector<ModMatrixPicker::Item>& ModMatrixLists::destinations() {
    if (!destinations_.built)
        buildDestinations();
    return destinations_.items;
}

juce::String ModMatrixLists::sourceLabel(int id) { return label(sources_, id, &ModMatrixLists::buildSources); }

juce::String ModMatrixLists::destinationLabel(int id) {
    return label(destinations_, id, &ModMatrixLists::buildDestinations);
}

// A slot bound elsewhere (a card drop, touch-to-add) adds a row without changing a node or a title, so an unknown id
// rebuilds once before it is called missing. It stays missing until the next invalidate, so a routing that is simply
// not listed costs no rebuild per tick.
juce::String ModMatrixLists::label(List& list, int id, void (ModMatrixLists::*build)()) {
    if (!list.built)
        (this->*build)();
    if (const auto it = list.labels.find(id); it != list.labels.end())
        return it->second;
    if (list.missing.count(id) > 0)
        return {};
    auto missing = std::move(list.missing);
    (this->*build)();
    list.missing = std::move(missing);
    if (const auto it = list.labels.find(id); it != list.labels.end())
        return it->second;
    list.missing.insert(id);
    return {};
}

void ModMatrixLists::index(List& list) {
    list.labels.clear();
    list.labels.reserve(list.items.size());
    for (const auto& item : list.items)
        list.labels[item.id] = item.text;
    list.built = true;
}

// The sources come from synth::ui::enumerateModSources, the list the mod dot's Add source list reads too. The closed
// box shows the row's own text, so a multi-output module's row carries the module name: never a bare jack name.
void ModMatrixLists::buildSources() {
    sources_.items.clear();
    const auto sources = enumerateModSources(graph_);
    size_t i = 0;
    while (i < sources.size()) {
        const auto& source = sources[i];
        size_t end = i;
        while (end < sources.size() && sources[end].category == source.category && sources[end].node == source.node)
            ++end;
        const juce::String category = flat_ ? juce::String() : modulationCategoryNames().at(source.category);
        for (size_t k = i; k < end; ++k) {
            const bool alone = !flat_ && end - i == 1;
            sources_.items.push_back({sources[k].itemId(), category,
                                      alone ? sources[k].moduleTitle : sources[k].label(), sources[k].aliases});
        }
        i = end;
    }
    index(sources_);
}

void ModMatrixLists::buildDestinations() {
    destinations_.items.clear();
    std::map<ModulationCategory, std::vector<juce::AudioProcessorGraph::Node*>> modulesByCategory;
    for (auto* node : graph_.getNodes())
        if (auto* module = dynamic_cast<ModuleBase*>(node->getProcessor()))
            modulesByCategory[module->getModulationCategory()].push_back(node);

    // A plugin's parameters are listed by hostedDestinations, modulated or not, so a bound slot has no row here: only
    // the label its closed box shows.
    std::vector<std::pair<int, juce::String>> hostedLabels;
    for (const auto& [category, modules] : modulesByCategory) {
        for (auto* node : modules) {
            const auto title = synth::moduleTitle(*node);
            if (dynamic_cast<HostedPluginModule*>(node->getProcessor()) != nullptr) {
                for (const auto& target : modDestinationCandidates(static_cast<ModuleBase*>(node->getProcessor())))
                    hostedLabels.emplace_back((int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex),
                                              title + " - " + target.name);
                continue;
            }
            for (const auto& target : modDestinationCandidates(static_cast<ModuleBase*>(node->getProcessor()))) {
                const int id = (int)((node->nodeID.uid << 8) | (uint32_t)target.channelIndex);
                destinations_.items.push_back({id, flat_ ? juce::String() : modulationCategoryNames().at(category),
                                               title + (flat_ ? ": " : " - ") + target.name});
            }
        }
    }
    index(destinations_);
    for (auto& [id, label] : hostedLabels)
        destinations_.labels[id] = std::move(label);
}

} // namespace synth::ui
