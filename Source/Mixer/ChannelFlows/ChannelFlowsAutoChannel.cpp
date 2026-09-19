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

// T184 (P9-3c, docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow"): BFS forward from `start`,
// following every outgoing graph edge (audio AND MIDI — an `AudioProcessorGraph::Connection` is always one or the
// other), to find every point where `start`'s own signal path reaches the output WITHOUT already
// passing through a `ChannelStripModule`. Each such point is returned as the exact `Connection`
// that crosses it — the caller (buildChannelForFeeds below) removes those edges and rebuilds a
// channel from their sources.
//
// Traversal rules:
//   - Never enter an `AttenuverterModule` node — `AudioEngine::addModRouting` always wraps a
//     hidden modulation leg in one of these; its own outgoing edge is a mod-CV destination
//     parameter, not part of `start`'s audio/MIDI signal path, and is neither traversed nor
//     itself an exit.
//   - Never expand PAST a `ChannelStripModule` — that branch already terminates in a channel, so
//     nothing downstream of it is `start`'s to claim. Not an exit either (it is not one of the
//     three terminal types below).
//   - Never expand past a terminal: Audio Output (`juce::AudioGraphIOProcessor` named "Audio
//     Output"), `RecordTapModule`, or `MasterModule`. An edge landing on one of these IS an exit
//     when it lands on the right channel — Audio Output/Rec Tap ch0/ch1, or Master's
//     `kDirectLeft`/`kDirectRight` (its ALREADY-channeled `kMixLeft`/`kMixRight` inputs are never
//     an exit — they can only be fed by an existing strip's own output, which this BFS never
//     reaches, having stopped at the strip).
//   - Every other node (an instrument, an FX module, a macro port pass-through, ...) is just
//     traversed through, exactly like any other hop in the chain.
//
// Cycle-safe (a visited-node set). Core cannot depend on AppUndoManager/GraphEditor, which is why
// this stays a pure query with no graph mutation (see ChannelFlows.h's declaration comment).
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
        // see above for why each of these stops traversal here.
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

            // A hidden modulation hop: never traversed, never an exit (see above).
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

// (GraphEditor::endConnectionDrag's T184 hook is today's only caller of this function.)
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

    // Each exit's original source now feeds the new channel's EQ input instead; AudioProcessorGraph
    // sums multiple sources landing on the same input channel, so more than one exit landing on the
    // same side (e.g. two separate Direct feeds) still sums exactly as it did before, just one hop
    // later — the sound does not change.
    return buildChannelChain(graph, leftFeeds, rightFeeds, layout);
}

} // namespace synth
