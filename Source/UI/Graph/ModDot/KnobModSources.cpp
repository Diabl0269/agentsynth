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

std::vector<KnobModSource> knobModSources(GraphEditor& editor, juce::AudioProcessorGraph::NodeID dest,
                                          int destChannel) {
    std::vector<KnobModSource> out;
    auto& graph = editor.getAudioEngine().getGraph();
    const auto isPort = [&editor](juce::AudioProcessorGraph::NodeID id) {
        return editor.getMacroController().nodeIsMacroPort(id);
    };
    for (const auto& info : editor.getCachedModDisplayInfo()) {
        if (info.destNodeID != dest || info.destChannelIndex != destChannel || info.attenuverterNodeID.uid == 0)
            continue;
        KnobModSource source;
        source.attenuverterId = info.attenuverterNodeID;
        source.amount = attenuverterAmount(graph, info.attenuverterNodeID, info.amount);
        source.bypassed = info.isBypassed;
        // ModulationDisplayInfo does not carry the source node: find the routing this entry came from.
        for (const auto& routing : editor.getCachedModRoutings()) {
            if (routing.attenuverterNodeID != info.attenuverterNodeID)
                continue;
            source.sourceNodeId = resolveRouting(graph, routing, isPort).source.node;
            if (auto* node = graph.getNodeForId(source.sourceNodeId))
                source.sourceName = synth::moduleTitle(*node);
            break;
        }
        out.push_back(std::move(source));
    }
    return out;
}

} // namespace synth::ui
