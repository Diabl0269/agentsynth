#include "KnobModSources.h"

#include "AudioEngine/AudioEngine.h"
#include "AudioEngine/ModuleTitle.h"
#include "Modules/ModuleBase.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/ModMatrixEndpoints.h"

namespace synth::ui {

float attenuverterAmount(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID attenuverterId,
                         float fallback) {
    if (auto* node = graph.getNodeForId(attenuverterId))
        if (auto* p = dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(node->getProcessor(), "amount")))
            return p->get();
    return fallback;
}

// Every routing is resolved against one cable index, so listing all chains costs what listing one knob's did.
std::vector<KnobModChain> knobModChains(GraphEditor& editor, bool fresh) {
    std::vector<KnobModChain> out;
    auto& graph = editor.getAudioEngine().getGraph();
    const auto isPort = [&editor](juce::AudioProcessorGraph::NodeID id) {
        return editor.getMacroController().nodeIsMacroPort(id);
    };
    std::vector<ModulationRouting> freshRoutings;
    std::vector<ModulationDisplayInfo> freshInfos;
    if (fresh) {
        freshRoutings = editor.getAudioEngine().getModulationRoutings();
        freshInfos = editor.getAudioEngine().getModulationDisplayInfo(freshRoutings);
    }
    const auto& routings = fresh ? freshRoutings : editor.getCachedModRoutings();
    const auto& infos = fresh ? freshInfos : editor.getCachedModDisplayInfo();
    // The destination is looked through macro ports like the source: a knob inside a macro is fed by a chain that
    // ends on an inlet, and the Mod Matrix and the mod dot both name that routing against the knob itself.
    const ConnectionIndex cables(graph);
    for (const auto& routing : routings) {
        if (routing.kind != ModulationRoutingKind::AttenuverterChain || routing.attenuverterNodeID.uid == 0 ||
            !routing.hasDest)
            continue;
        const auto real = resolveRouting(cables, routing, isPort);
        KnobModChain chain;
        chain.card = real.dest.node;
        chain.destChannel = real.dest.channel;
        auto& source = chain.source;
        source.attenuverterId = routing.attenuverterNodeID;
        source.amount = attenuverterAmount(graph, routing.attenuverterNodeID, 1.0f);
        source.bypassed = routing.isBypassed;
        for (const auto& info : infos)
            if (info.attenuverterNodeID == routing.attenuverterNodeID) {
                source.bypassed = info.isBypassed;
                break;
            }
        source.sourceNodeId = real.source.node;
        source.sourceChannel = real.source.channel;
        if (auto* node = graph.getNodeForId(source.sourceNodeId))
            source.sourceName = synth::moduleTitle(*node);
        out.push_back(std::move(chain));
    }
    return out;
}

std::vector<KnobModSource> knobModSources(GraphEditor& editor, juce::AudioProcessorGraph::NodeID dest, int destChannel,
                                          bool fresh) {
    std::vector<KnobModSource> out;
    for (auto& chain : knobModChains(editor, fresh))
        if (chain.card == dest && chain.destChannel == destChannel)
            out.push_back(std::move(chain.source));
    return out;
}

std::map<std::pair<juce::uint32, int>, int> countKnobModSources(GraphEditor& editor,
                                                                const std::vector<ModulationRouting>& routings) {
    std::map<std::pair<juce::uint32, int>, int> counts;
    const ConnectionIndex cables(editor.getAudioEngine().getGraph());
    const auto isPort = [&editor](juce::AudioProcessorGraph::NodeID id) {
        return editor.getMacroController().nodeIsMacroPort(id);
    };
    for (const auto& routing : routings) {
        if (routing.kind != ModulationRoutingKind::AttenuverterChain || routing.attenuverterNodeID.uid == 0 ||
            !routing.hasDest)
            continue;
        const auto real = resolveRouting(cables, routing, isPort);
        ++counts[{real.dest.node.uid, real.dest.channel}];
    }
    return counts;
}

} // namespace synth::ui

namespace synth::ui {

KnobModTarget knobModTarget(GraphEditor& editor, juce::AudioProcessorGraph::NodeID card, int destChannel) {
    auto* node = editor.getAudioEngine().getGraph().getNodeForId(card);
    auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
    if (module == nullptr)
        return {};
    for (const auto& target : module->getModulationTargets()) {
        if (target.channelIndex != destChannel)
            continue;
        if (const auto* param = module->parameterForModTarget(target))
            return {param->paramID, param->getName(100)};
        return {target.paramId, target.name};
    }
    return {};
}

} // namespace synth::ui
