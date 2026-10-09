// PortTargetList.cpp -- which jacks and knobs a jack may be connected to, and which module types the search can make.
// docs/layout/cables.md#port-connections-panel.

#include "PortTargetList.h"

#include "AI/AIStateMapper/AIStateMapper.h"
#include "AudioEngine/AudioEngine.h"
#include "Modules/ModuleBase.h"
#include "PortConnector.h"
#include "UI/Graph/MacroGroupController/MacroGroupController.h"
#include "UI/Graph/ModDot/KnobModSources.h"
#include "UI/Graph/ModuleComponent/ModuleComponent.h"
#include "UI/Layout/SearchMatch.h"
#include <algorithm>
#include <map>
#include <typeindex>

namespace synth::ui {

namespace {

// A modulation output: its jack carries a parameter-CV signal (the test that offers the first-use "drop it on a knob"
// hint), so its knobs are offered besides the jacks.
bool isModulationOutput(GraphEditor& editor, const PortRef& port) {
    if (port.isInput || port.isMidi)
        return false;
    auto* node = editor.getAudioEngine().getGraph().getNodeForId(port.node);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return false;
    const auto targets = module->getJackTargets(port.jack, /*isInput=*/false);
    return std::any_of(targets.begin(), targets.end(), [](const auto& t) { return t.role == PortRole::ModCV; });
}

// Whether a cable already runs from the panel's jack to `target`.
bool isWired(const std::vector<PortConnection>& wired, const PortTarget& target) {
    return std::any_of(wired.begin(), wired.end(), [&target](const PortConnection& c) {
        if (c.far.node != target.node)
            return false;
        return target.kind == PortTarget::Kind::Knob ? c.farRawChannel == target.knobChannel && !c.far.isMidi
                                                     : c.far == target.jack;
    });
}

PortTarget makeJack(GraphEditor& editor, juce::AudioProcessorGraph::NodeID node, const juce::String& title,
                    PortRef jack) {
    PortTarget target;
    target.kind = PortTarget::Kind::Jack;
    target.node = node;
    target.jack = jack;
    target.moduleTitle = title;
    target.portName = jackName(editor, jack);
    target.aliases = moduleSearchAliases(title);
    return target;
}

} // namespace

std::vector<PortTargetModule> listPortTargets(GraphEditor& editor, const PortRef& port) {
    std::vector<PortTargetModule> out;
    auto& graph = editor.getAudioEngine().getGraph();
    auto* ownCard = [&]() -> ModuleComponent* {
        for (auto* card : editor.getModuleComponents())
            if (card != nullptr && card->getNodeId() == port.node)
                return card;
        return nullptr;
    }();
    const auto ownCentre = ownCard != nullptr ? ownCard->getBounds().getCentre() : juce::Point<int>();
    const auto wired = listPortConnections(editor, port);
    const bool knobs = isModulationOutput(editor, port);

    struct Ranked {
        PortTargetModule module;
        float distance = 0.0f;
    };
    std::vector<Ranked> ranked;
    for (auto* card : editor.getModuleComponents()) {
        if (card == nullptr || !card->isVisible() || card->getNodeId() == port.node)
            continue;
        const auto id = card->getNodeId();
        auto* node = graph.getNodeForId(id);
        if (node == nullptr || editor.getMacroController().nodeIsMacroPort(id))
            continue;
        auto* processor = node->getProcessor();
        auto* module = dynamic_cast<ModuleBase*>(processor);
        if (module != nullptr && module->getModuleType() == ModuleType::Attenuverter)
            continue;

        Ranked entry;
        entry.module.node = id;
        entry.module.title = editor.getModuleTitle(id, processor);
        auto add = [&](PortTarget target) {
            target.connected = isWired(wired, target);
            entry.module.targets.push_back(std::move(target));
        };
        const bool wantInputs = !port.isInput;
        if (port.isMidi) {
            if (wantInputs ? processor->acceptsMidi() : processor->producesMidi())
                add(makeJack(editor, id, entry.module.title,
                             {id, juce::AudioProcessorGraph::midiChannelIndex, wantInputs, true}));
        } else if (wantInputs) {
            for (const int i : card->drawnInputJackIndices())
                add(makeJack(editor, id, entry.module.title, {id, i, true, false}));
        } else {
            const int outs =
                module != nullptr ? module->getVisibleOutputPortCount() : processor->getTotalNumOutputChannels();
            for (int i = 0; i < outs; ++i)
                add(makeJack(editor, id, entry.module.title, {id, i, false, false}));
        }
        if (knobs && module != nullptr) {
            std::vector<int> seen;
            for (const auto& t : module->getModulationTargets()) {
                if (std::find(seen.begin(), seen.end(), t.channelIndex) != seen.end())
                    continue;
                seen.push_back(t.channelIndex);
                PortTarget target;
                target.kind = PortTarget::Kind::Knob;
                target.node = id;
                target.knobChannel = t.channelIndex;
                target.moduleTitle = entry.module.title;
                target.portName = knobModTarget(editor, id, t.channelIndex).paramName;
                if (target.portName.isEmpty())
                    target.portName = t.name;
                target.aliases = moduleSearchAliases(entry.module.title);
                add(std::move(target));
            }
        }
        if (entry.module.targets.empty())
            continue;
        const auto centre = card->getBounds().getCentre();
        entry.distance = (float)centre.getDistanceFrom(ownCentre);
        ranked.push_back(std::move(entry));
    }
    std::stable_sort(ranked.begin(), ranked.end(), [](const Ranked& a, const Ranked& b) {
        if (a.distance != b.distance)
            return a.distance < b.distance;
        return a.module.title.compareNatural(b.module.title) < 0;
    });
    for (auto& r : ranked)
        out.push_back(std::move(r.module));
    return out;
}

std::vector<PortTarget> listNewModuleTargets(const PortRef& port) {
    // Probed once per kind of jack (the answer depends on direction and MIDI, never on which jack): one throwaway
    // instance per authorable type, like newModuleSources(), so a new module type shows up without an edit here.
    static std::map<std::pair<bool, bool>, std::vector<PortTarget>> cache;
    const std::pair<bool, bool> key{port.isInput, port.isMidi};
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    std::vector<PortTarget> out;
    std::vector<std::type_index> seen;
    for (const auto& name : synth::AIStateMapper::authorableModuleTypes()) {
        if (GraphEditor::isSingletonIOModule(name))
            continue;
        auto probe = synth::AIStateMapper::createModule(name);
        auto* module = dynamic_cast<ModuleBase*>(probe.get());
        if (module == nullptr || module->getModuleType() == ModuleType::Attenuverter)
            continue;
        const std::type_index moduleClass(typeid(*module));
        if (const auto twin = std::find(seen.begin(), seen.end(), moduleClass); twin != seen.end()) {
            out[(size_t)(twin - seen.begin())].aliases += " " + name; // another key of an offered class: a search word
            continue;
        }
        if (PortConnector::firstCompatibleJack(name, port) < 0)
            continue;
        seen.push_back(moduleClass);
        PortTarget target;
        target.kind = PortTarget::Kind::NewModule;
        target.moduleTitle = "New " + name;
        target.newType = name;
        target.aliases = name + " " + moduleSearchAliases(name);
        out.push_back(std::move(target));
    }
    cache[key] = out;
    return out;
}

const PortTarget* findJackTarget(const std::vector<PortTargetModule>& modules, const PortRef& jack) {
    for (const auto& m : modules)
        for (const auto& t : m.targets)
            if (t.kind == PortTarget::Kind::Jack && t.jack == jack)
                return &t;
    return nullptr;
}

const PortTarget* findKnobTarget(const std::vector<PortTargetModule>& modules, juce::AudioProcessorGraph::NodeID node,
                                 int channel) {
    for (const auto& m : modules)
        for (const auto& t : m.targets)
            if (t.kind == PortTarget::Kind::Knob && t.node == node && t.knobChannel == channel)
                return &t;
    return nullptr;
}

} // namespace synth::ui
