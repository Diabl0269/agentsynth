#include "SidechainConnections.h"

#include "../Modules/ModuleBase.h"
#include <set>

namespace synth {

// JUCE's graph never tells a processor which of its inputs are connected, and the key's LEVEL cannot
// stand in for that (a silent kick between hits is still a plugged key). So connectivity is read off
// the connection list here, on the message thread, and handed to each module as one relaxed atomic
// its processBlock loads once per block. A flat pass is enough: a cable arriving through a macro port
// is itself a connection landing on the key channel.
void publishSidechainConnections(juce::AudioProcessorGraph& graph) {
    std::set<juce::AudioProcessorGraph::NodeID> keyed;
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.isMIDI())
            continue;
        auto* node = graph.getNodeForId(conn.destination.nodeID);
        auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module != nullptr && module->mapInputChannel(conn.destination.channelIndex).role == PortRole::Sidechain)
            keyed.insert(conn.destination.nodeID);
    }

    for (auto* node : graph.getNodes())
        if (auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr)
            module->setSidechainConnected(keyed.count(node->nodeID) > 0);
}

} // namespace synth
