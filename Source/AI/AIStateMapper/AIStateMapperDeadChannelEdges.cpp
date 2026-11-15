// AIStateMapper — saved cables on a raw channel that carries nothing, on the APPLY side.
//
// A module may keep raw input channels that no jack heads (the mono Oscillator's 7-12, poly's voice 8 and shared CV).
// While such a channel still headed a jack beside the real target, a cable arriving from an audio-role jack (a track's
// Macro In) could land on it: the card drew it on the right jack, but it moved nothing (a Glide cable with no ring).
// Projects saved then hold that edge. It is re-pointed onto the one channel that jack really heads, so the cable the
// person sees is the one that plays.

#include "AIStateMapperInternal.h"
#include <vector>

namespace synth {
namespace detail {

using Graph = juce::AudioProcessorGraph;

void repointDeadChannelEdges(Graph& graph) {
    std::vector<std::pair<Graph::Connection, int>> moves;
    for (const auto& c : graph.getConnections()) {
        if (c.destination.isMIDI())
            continue;
        auto* far = graph.getNodeForId(c.destination.nodeID);
        auto* mb = far != nullptr ? dynamic_cast<ModuleBase*>(far->getProcessor()) : nullptr;
        if (mb == nullptr)
            continue;
        const int raw = c.destination.channelIndex;
        if (mb->isReservedInputChannel(raw) || mb->isAutoPromotableModTarget(raw))
            continue; // a channel kept for a later cable, or a knob's own: never dead
        const auto port = mb->mapInputChannel(raw);
        if (port.isPolyGroupHead || port.role != PortRole::Other ||
            port.visibleJackIndex >= mb->getVisibleInputPortCount())
            continue;
        const auto targets = mb->getJackTargets(port.visibleJackIndex, /*isInput*/ true);
        if (targets.size() != 1 || targets.front().voiceSpan != 1 || targets.front().role == PortRole::Other ||
            targets.front().rawHeadChannel == raw)
            continue;
        moves.emplace_back(c, targets.front().rawHeadChannel);
    }
    for (const auto& [c, raw] : moves) {
        graph.removeConnection(c, Graph::UpdateKind::none);
        const Graph::Connection moved{c.source, {c.destination.nodeID, raw}};
        if (!graph.isConnected(moved))
            graph.addConnection(moved, Graph::UpdateKind::none);
    }
}

// The two-leg repair runs first: it moves a leg onto a channel this pass then leaves alone.
void repairSavedJackEdges(juce::AudioProcessorGraph& graph) {
    repointFannedTwoLegJackEdges(graph);
    repointDeadChannelEdges(graph);
}

} // namespace detail
} // namespace synth
