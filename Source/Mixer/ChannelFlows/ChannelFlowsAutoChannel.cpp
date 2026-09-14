// Concern: T184 auto-channel -- finding a chain's unchanneled output feeds and rebuilding a
// channel from them (findUnchanneledOutputFeeds / buildChannelForFeeds).
#include "ChannelFlows.h"

#include "ChannelFlowsInternal.h"
#include "Modules/AttenuverterModule.h"
#include "Modules/ChannelStripModule.h"
#include "Modules/MasterModule.h"
#include "Modules/RecordTapModule.h"
#include <algorithm>

namespace synth {

std::vector<juce::AudioProcessorGraph::Connection> findUnchanneledOutputFeeds(juce::AudioProcessorGraph& graph,
                                                                              juce::AudioProcessorGraph::NodeID start) {
    std::vector<juce::AudioProcessorGraph::Connection> exits;

    std::vector<juce::AudioProcessorGraph::NodeID> visited{start};
    std::vector<juce::AudioProcessorGraph::NodeID> queue{start};

    while (!queue.empty()) {
        const auto nodeId = queue.front();
        queue.erase(queue.begin());

        auto* node = graph.getNodeForId(nodeId);
        if (node == nullptr)
            continue;
        auto* processor = node->getProcessor();
        if (processor == nullptr)
            continue;

        // Never expand PAST a terminal, an already-channeled branch, or a hidden modulation hop —
        // see the header comment for why each of these stops traversal here.
        if (dynamic_cast<RecordTapModule*>(processor) != nullptr || dynamic_cast<MasterModule*>(processor) != nullptr ||
            dynamic_cast<ChannelStripModule*>(processor) != nullptr ||
            dynamic_cast<AttenuverterModule*>(processor) != nullptr || processor->getName() == "Audio Output")
            continue;

        for (const auto& conn : graph.getConnections()) {
            if (conn.source.nodeID != nodeId)
                continue;

            auto* destNode = graph.getNodeForId(conn.destination.nodeID);
            if (destNode == nullptr)
                continue;
            auto* destProcessor = destNode->getProcessor();
            if (destProcessor == nullptr)
                continue;

            // A hidden modulation hop: never traversed, never an exit (see header comment).
            if (dynamic_cast<AttenuverterModule*>(destProcessor) != nullptr)
                continue;

            const int channel = conn.destination.channelIndex;

            if (destProcessor->getName() == "Audio Output") {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Audio Output
            }
            if (dynamic_cast<RecordTapModule*>(destProcessor) != nullptr) {
                if (channel == 0 || channel == 1)
                    exits.push_back(conn);
                continue; // terminal — never expand past Rec Tap
            }
            if (dynamic_cast<MasterModule*>(destProcessor) != nullptr) {
                // kMixLeft/kMixRight are NOT an exit — only a strip's own output can land there,
                // and this BFS never reaches one (it stops at a ChannelStripModule below).
                if (channel == MasterModule::kDirectLeft || channel == MasterModule::kDirectRight)
                    exits.push_back(conn);
                continue; // terminal — never expand past Master
            }
            if (dynamic_cast<ChannelStripModule*>(destProcessor) != nullptr)
                continue; // already channeled — do not expand past it, and not an exit itself

            if (std::find(visited.begin(), visited.end(), conn.destination.nodeID) == visited.end()) {
                visited.push_back(conn.destination.nodeID);
                queue.push_back(conn.destination.nodeID);
            }
        }
    }

    return exits;
}

DefaultChannel buildChannelForFeeds(juce::AudioProcessorGraph& graph,
                                    const std::vector<juce::AudioProcessorGraph::Connection>& exits,
                                    const DefaultChannelLayout& layout) {
    if (exits.empty())
        return {};

    // Classify by DESTINATION channel before anything is removed — ch0/kDirectLeft -> Left,
    // ch1/kDirectRight -> Right (the only two channel numbers findUnchanneledOutputFeeds ever
    // returns an exit for).
    std::vector<juce::AudioProcessorGraph::NodeAndChannel> leftFeeds, rightFeeds;
    for (const auto& exit : exits) {
        const int channel = exit.destination.channelIndex;
        const bool isRight = (channel == 1) || (channel == MasterModule::kDirectRight);
        (isRight ? rightFeeds : leftFeeds).push_back(exit.source);
    }

    // REMOVE FIRST, then build — same "collect, then mutate" reasoning spliceMasterNode's own
    // splice uses (removeConnection while iterating the list it came from would invalidate it).
    for (const auto& exit : exits)
        graph.removeConnection(exit);

    return buildChannelChain(graph, leftFeeds, rightFeeds, layout);
}

} // namespace synth
