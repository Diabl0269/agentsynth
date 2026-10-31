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

// The sources the silence rule covers. A processor's bypass passes its input on and a hosted plugin's Level is not
// known, so neither can promise zeros.
bool isSoundSource(const ModuleBase& module) {
    switch (module.getModuleType()) {
    case ModuleType::Oscillator:
    case ModuleType::Wavetable:
    case ModuleType::Noise:
    case ModuleType::Pluck:
        return true;
    default:
        return false;
    }
}

bool levelIsZero(const ModuleBase& module) {
    for (auto* param : module.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*>(param);
            withId != nullptr && withId->paramID == "level")
            if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
                return ranged->convertFrom0to1(ranged->getValue()) <= 0.0f;
    return false;
}

bool isOff(const ModuleBase& module) { return module.isBypassed() || (module.hasMuteParameter() && module.isMuted()); }

// Set when a Level CV could lift this source off zero, so Level 0 alone does not make it silent.
bool hasLevelCable(const ModuleBase& module, NodeID nodeId,
                   const std::vector<juce::AudioProcessorGraph::Connection>& all) {
    for (const auto& target : module.getModulationTargets())
        if (target.paramId == "level")
            for (const auto& conn : all)
                if (conn.destination.nodeID == nodeId && conn.destination.channelIndex == target.channelIndex)
                    return true;
    return false;
}
} // namespace

std::set<NodeID> findSilentSources(juce::AudioProcessorGraph& graph) {
    std::set<NodeID> silent;
    const auto connections = graph.getConnections();
    for (auto* node : graph.getNodes()) {
        const auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module == nullptr || !isSoundSource(*module))
            continue;
        if (isOff(*module) || (levelIsZero(*module) && !hasLevelCable(*module, node->nodeID, connections)))
            silent.insert(node->nodeID);
    }
    return silent;
}

std::uint64_t silentSourcesSignature(juce::AudioProcessorGraph& graph) {
    std::uint64_t signature = 14695981039346656037ull; // FNV-1a
    for (auto* node : graph.getNodes()) {
        const auto* module = node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
        if (module == nullptr || !isSoundSource(*module))
            continue;
        const std::uint64_t state =
            (std::uint64_t{node->nodeID.uid} << 2) | (isOff(*module) ? 1u : 0u) | (levelIsZero(*module) ? 2u : 0u);
        signature = (signature ^ state) * 1099511628211ull;
    }
    return signature;
}

// JUCE's graph sums every edge landing on one input channel at unity and has no per-edge gain, so a
// stereo pair cabled into a mono jack cannot be averaged in the graph itself. Instead the pairs are
// read off the connection list here and each module scales its own marked channels once per block
// (ModuleBase::applyInputDownMix) -- render-time, never an edge, like L/Mono normalling. A silent
// source's feed is left out, so cabling one next to a pair leaves that pair averaged.
void publishStereoDownMix(juce::AudioProcessorGraph& graph) {
    const auto silent = findSilentSources(graph);
    std::map<NodeID, std::map<int, std::vector<Feed>>> feedsByDest;
    for (const auto& conn : graph.getConnections()) {
        if (conn.destination.isMIDI() || silent.count(conn.source.nodeID) > 0)
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
