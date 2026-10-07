// AIStateMapper — modulation destinations and plan-scoped node ids on the APPLY side.
//
// applyJSONToGraph's "modulations" step lives here, with the two ways a modulation names its target
// ("destPort", or "destParam" resolved through ModuleBase::modulationChannelForParam), and the
// PatchIdScope plumbing an edit plan uses to let a merge patch refer to nodes an earlier step of the
// same plan created. The validation side of both stays in AIStateMapperValidation.cpp.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"
#include "AudioEngine/ConnectionIndex.h"
#include <algorithm>
#include <set>
#include <tuple>

namespace synth {
namespace detail {

// A bound node is reachable through its plan id only, and a hidden one not at all: the model wrote
// its patch against a graph in which neither existed, so their auto-assigned uids mean nothing to
// it. Letting a patch address them by raw uid would make "id 57" an edit-in-place at apply time
// (the node exists) but a brand-new node in the preview (it does not), which is exactly the
// preview/apply split the plan's shared code exists to rule out.
bool scopeHidesNode(const PatchIdScope* scope, juce::AudioProcessorGraph::NodeID nodeId) {
    if (scope == nullptr)
        return false;
    if (scope->hiddenNodes.count(nodeId) > 0)
        return true;
    for (const auto& [planId, bound] : scope->boundIds)
        if (bound == nodeId)
            return true;
    return false;
}

// Without a scope this is the identity map over live uids it has always been, so a plain merge
// patch resolves exactly as before.
void seedMergeIdMap(const juce::AudioProcessorGraph& graph, const PatchIdScope* scope, NodeIdMap& idMap) {
    for (auto* node : graph.getNodes())
        if (!scopeHidesNode(scope, node->nodeID))
            idMap[(int)node->nodeID.uid] = node->nodeID;
    if (scope == nullptr)
        return;
    for (const auto& [planId, nodeId] : scope->boundIds)
        if (graph.getNodeForId(nodeId) != nullptr)
            idMap[(int)planId] = nodeId;
}

// A removal goes through the id map, so a plan id removes the node it is bound to. An id the map
// does not hold falls back to the raw uid exactly as before (a no-op for a node that does not
// exist), except that a scope-hidden node is never reachable that way.
std::optional<juce::AudioProcessorGraph::NodeID> removalTarget(const NodeIdMap& idMap, const PatchIdScope* scope,
                                                               int patchId) {
    if (auto it = idMap.find(patchId); it != idMap.end())
        return it->second;
    const auto raw = juce::AudioProcessorGraph::NodeID((juce::uint32)patchId);
    if (scopeHidesNode(scope, raw))
        return std::nullopt;
    return raw;
}

// validatePatch has already proven, on the untrusted path, that "destParam" resolves and agrees
// with any "destPort". Resolving it again here, against the processor the destination id now
// denotes, is what turns the parameter name into the channel the attenuverter must feed. A trusted
// patch (our own snapshots) never carries "destParam"; an absent "destPort" is read as port 0
// there, as it always has been.
std::optional<int> modulationDestPort(const juce::DynamicObject& modulation, juce::AudioProcessor* destProcessor) {
    if (!modulation.hasProperty("destParam"))
        return (int)modulation.getProperty("destPort");
    auto* module = dynamic_cast<ModuleBase*>(destProcessor);
    const int channel =
        module != nullptr ? module->modulationChannelForParam(modulation.getProperty("destParam").toString()) : -1;
    if (channel < 0)
        return std::nullopt;
    return channel;
}

namespace {

// One attenuverter routing: source:sourcePort -> (attenuverter) -> dest:destPort.
using RoutingKey = std::tuple<juce::uint32, int, juce::uint32, int>;

// Every routing an attenuverter already carries (e.g. one the same patch's nodes/connections arrays restated), so a
// modulation is not doubled. Built from one ConnectionIndex for the whole modulations array: asking the live graph per
// modulation scanned every node and re-sorted every cable for each one, O(modulations x nodes x cables) per paste.
std::set<RoutingKey> existingRoutings(const juce::AudioProcessorGraph& graph) {
    const ConnectionIndex cables(graph);
    std::set<RoutingKey> routings;
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) == nullptr)
            continue;
        for (const auto& in : cables.into(node->nodeID)) {
            if (in.destination.channelIndex != 0)
                continue;
            for (const auto& out : cables.outOf(node->nodeID))
                if (out.source.channelIndex == 0)
                    routings.insert({in.source.nodeID.uid, in.source.channelIndex, out.destination.nodeID.uid,
                                     out.destination.channelIndex});
        }
    }
    return routings;
}

} // namespace

// The cables are indexed once per entry rather than re-sorted per attenuverter; the first match in graph node order
// wins, as it always has.
std::optional<juce::AudioProcessorGraph::NodeID> findModulationAttenuverter(const juce::AudioProcessorGraph& graph,
                                                                            juce::AudioProcessorGraph::NodeID source,
                                                                            juce::AudioProcessorGraph::NodeID dest,
                                                                            int destPort) {
    const ConnectionIndex cables(graph);
    for (auto* node : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(node->getProcessor()) == nullptr)
            continue;
        const auto& in = cables.into(node->nodeID);
        const auto& out = cables.outOf(node->nodeID);
        const bool sourceMatch = std::any_of(in.begin(), in.end(), [&](const auto& c) {
            return c.destination.channelIndex == 0 && c.source.nodeID == source;
        });
        const bool destMatch = std::any_of(out.begin(), out.end(), [&](const auto& c) {
            return c.source.channelIndex == 0 && c.destination.nodeID == dest && c.destination.channelIndex == destPort;
        });
        if (sourceMatch && destMatch)
            return node->nodeID;
    }
    return std::nullopt;
}

void applyModulationEntries(const juce::Array<juce::var>& modulations, juce::AudioProcessorGraph& graph,
                            NodeIdMap& idMap) {
    auto routings = existingRoutings(graph); // kept current as routings are added below
    for (const auto& modVar : modulations) {
        auto* modObj = modVar.getDynamicObject();
        if (modObj == nullptr)
            continue;
        int sourceId = (int)modObj->getProperty("source");
        int destId = (int)modObj->getProperty("dest");
        int sourcePort = modObj->hasProperty("sourcePort") ? (int)modObj->getProperty("sourcePort") : 0;
        float amount = modObj->hasProperty("amount") ? (float)modObj->getProperty("amount") : 1.0f;
        bool bypass = modObj->hasProperty("bypass") ? (bool)modObj->getProperty("bypass") : false;

        if (!idMap.count(sourceId) || !idMap.count(destId))
            continue;
        auto mappedSource = idMap[sourceId];
        auto mappedDest = idMap[destId];

        auto* destNode = graph.getNodeForId(mappedDest);
        const auto destPort = modulationDestPort(*modObj, destNode != nullptr ? destNode->getProcessor() : nullptr);
        if (!destPort.has_value())
            continue;

        const RoutingKey key{mappedSource.uid, sourcePort, mappedDest.uid, *destPort};
        if (routings.count(key) != 0)
            continue;

        auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>(), std::nullopt,
                                       juce::AudioProcessorGraph::UpdateKind::none);
        if (!attenNode)
            continue;
        if (auto* param =
                dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(attenNode->getProcessor(), "amount")))
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(amount));
        if (bypass)
            if (auto* bp =
                    dynamic_cast<juce::AudioParameterBool*>(findParameterByID(attenNode->getProcessor(), "bypassed")))
                bp->setValueNotifyingHost(1.0f);

        const bool in = graph.addConnection({{mappedSource, sourcePort}, {attenNode->nodeID, 0}},
                                            juce::AudioProcessorGraph::UpdateKind::none);
        const bool out = graph.addConnection({{attenNode->nodeID, 0}, {mappedDest, *destPort}},
                                             juce::AudioProcessorGraph::UpdateKind::none);
        if (in && out)
            routings.insert(key);
    }
}

} // namespace detail
} // namespace synth
