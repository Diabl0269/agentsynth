#include "StereoDownMix.h"

#include "../Modules/ModuleBase.h"
#include <algorithm>
#include <cstdint>
#include <map>
#include <utility>
#include <vector>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
using Feed = std::pair<NodeID, int>; // source node, source raw output channel

// The raw output channel that pairs with `channel` on `source` as its other stereo leg, or -1.
// A ModuleBase pairs voice v's Left (raw v) with its Right (rightAudioLegChannel() + v) -- ch0/ch1
// for the FX, the kRightBase block for the split-block voice modules. Anything else (the graph's
// Audio Input node) is a plain interleaved device pair, ch0/ch1.
int partnerLeg(juce::AudioProcessor* source, int channel) {
    if (auto* module = dynamic_cast<ModuleBase*>(source)) {
        const int right = module->rightAudioLegChannel();
        if (right <= 0)
            return -1;
        return channel < right ? right + channel : channel - right;
    }
    return channel == 0 ? 1 : channel == 1 ? 0 : -1;
}

// True iff every feed's partner leg is also a feed: the channel carries nothing but whole L+R pairs.
bool onlyWholePairs(juce::AudioProcessorGraph& graph, const std::vector<Feed>& feeds) {
    for (const auto& [sourceId, channel] : feeds) {
        auto* node = graph.getNodeForId(sourceId);
        const int partner = node != nullptr ? partnerLeg(node->getProcessor(), channel) : -1;
        if (partner < 0 || std::find(feeds.begin(), feeds.end(), Feed{sourceId, partner}) == feeds.end())
            return false;
    }
    return !feeds.empty();
}
} // namespace

// JUCE's graph sums every edge landing on one input channel at unity and has no per-edge gain, so a
// stereo pair cabled into a mono jack cannot be averaged in the graph itself. Instead the pairs are
// read off the connection list here and each module scales its own marked channels once per block
// (ModuleBase::applyInputDownMix) -- render-time, never an edge, like L/Mono normalling.
void publishStereoDownMix(juce::AudioProcessorGraph& graph) {
    std::map<NodeID, std::map<int, std::vector<Feed>>> feedsByDest;
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.isMIDI())
            continue;
        feedsByDest[conn.destination.nodeID][conn.destination.channelIndex].push_back(
            {conn.source.nodeID, conn.source.channelIndex});
    }

    for (auto* node : graph.getNodes()) {
        auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module == nullptr)
            continue;
        std::uint64_t mask = 0;
        if (const auto it = feedsByDest.find(node->nodeID); it != feedsByDest.end())
            for (const auto& [channel, feeds] : it->second)
                if (channel < ModuleBase::kMaxDownMixChannels && onlyWholePairs(graph, feeds))
                    mask |= std::uint64_t{1} << channel;
        module->setInputDownMixMask(mask);
    }
}

} // namespace synth
