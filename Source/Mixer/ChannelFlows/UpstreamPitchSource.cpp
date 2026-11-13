// UpstreamPitchSource.cpp -- finds the Pitch cable feeding a voice chain, for the Custom filter's per-note modes.
#include "UpstreamPitchSource.h"
#include "Modules/ModuleBase.h"
#include <deque>
#include <set>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;

ModuleBase* moduleOf(const juce::AudioProcessorGraph& graph, NodeID id) {
    auto* node = graph.getNodeForId(id);
    return node != nullptr ? dynamic_cast<ModuleBase*>(node->getProcessor()) : nullptr;
}
} // namespace

bool hasPitchCable(const ConnectionIndex& cables, const juce::AudioProcessorGraph& graph, NodeID module) {
    auto* base = moduleOf(graph, module);
    if (base == nullptr)
        return false;
    for (const auto& c : cables.into(module))
        if (c.destination.channelIndex != juce::AudioProcessorGraph::midiChannelIndex &&
            base->mapInputChannel(c.destination.channelIndex).role == PortRole::Pitch)
            return true;
    return false;
}

std::optional<PitchSource> upstreamPitchSource(const ConnectionIndex& cables, const juce::AudioProcessorGraph& graph,
                                               NodeID module) {
    std::deque<NodeID> queue{module};
    std::set<juce::uint32> seen{module.uid};
    while (!queue.empty()) {
        const NodeID at = queue.front();
        queue.pop_front();
        auto* base = moduleOf(graph, at);
        if (base == nullptr)
            continue;
        for (const auto& c : cables.into(at)) {
            if (c.destination.channelIndex == juce::AudioProcessorGraph::midiChannelIndex)
                continue;
            const auto port = base->mapInputChannel(c.destination.channelIndex);
            auto* source = moduleOf(graph, c.source.nodeID);
            const auto out = source != nullptr ? source->mapOutputChannel(c.source.channelIndex) : LogicalPort{};
            // A mono Oscillator's pitch jack is a plain CV input, so the cable also counts when a Pitch output feeds
            // it.
            const bool carriesPitch = (port.role == PortRole::Pitch && port.isPolyGroupHead) ||
                                      (out.role == PortRole::Pitch && out.isPolyGroupHead);
            if (carriesPitch && at != module) {
                if (source != nullptr)
                    return PitchSource{c.source.nodeID, out.visibleJackIndex};
            } else if (port.role == PortRole::Audio && seen.insert(c.source.nodeID.uid).second) {
                queue.push_back(c.source.nodeID);
            }
        }
    }
    return std::nullopt;
}

} // namespace synth
