// AIStateMapper — modulation destinations and plan-scoped node ids on the APPLY side.
//
// applyJSONToGraph's "modulations" step lives here, with the two ways a modulation names its target
// ("destPort", or "destParam" resolved through ModuleBase::modulationChannelForParam), and the
// PatchIdScope plumbing an edit plan uses to let a merge patch refer to nodes an earlier step of the
// same plan created. The validation side of both stays in AIStateMapperValidation.cpp.

#include "AIStateMapper.h"

#include "AIStateMapperInternal.h"

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

// Whether an attenuverter already carries source:sourcePort -> dest:destPort (e.g. one the same
// patch's nodes/connections arrays restated), so the modulation is not doubled.
bool routingExists(const juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID source, int sourcePort,
                   juce::AudioProcessorGraph::NodeID dest, int destPort) {
    for (auto* existingNode : graph.getNodes()) {
        if (dynamic_cast<AttenuverterModule*>(existingNode->getProcessor()) == nullptr)
            continue;
        bool srcMatch = false, dstMatch = false;
        for (const auto& conn : graph.getConnections()) {
            if (conn.destination.nodeID == existingNode->nodeID && conn.destination.channelIndex == 0 &&
                conn.source.nodeID == source && conn.source.channelIndex == sourcePort)
                srcMatch = true;
            if (conn.source.nodeID == existingNode->nodeID && conn.source.channelIndex == 0 &&
                conn.destination.nodeID == dest && conn.destination.channelIndex == destPort)
                dstMatch = true;
        }
        if (srcMatch && dstMatch)
            return true;
    }
    return false;
}

} // namespace

void applyModulationEntries(const juce::Array<juce::var>& modulations, juce::AudioProcessorGraph& graph,
                            NodeIdMap& idMap) {
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

        if (routingExists(graph, mappedSource, sourcePort, mappedDest, *destPort))
            continue;

        auto attenNode = graph.addNode(std::make_unique<AttenuverterModule>());
        if (!attenNode)
            continue;
        if (auto* param =
                dynamic_cast<juce::AudioParameterFloat*>(findParameterByID(attenNode->getProcessor(), "amount")))
            param->setValueNotifyingHost(param->getNormalisableRange().convertTo0to1(amount));
        if (bypass)
            if (auto* bp =
                    dynamic_cast<juce::AudioParameterBool*>(findParameterByID(attenNode->getProcessor(), "bypassed")))
                bp->setValueNotifyingHost(1.0f);

        graph.addConnection({{mappedSource, sourcePort}, {attenNode->nodeID, 0}});
        graph.addConnection({{attenNode->nodeID, 0}, {mappedDest, *destPort}});
    }
}

} // namespace detail
} // namespace synth
