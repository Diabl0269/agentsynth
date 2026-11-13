// Concern: MixerGraphView (MixerModelInternal.h) -- the one read-only view of the graph a mixer snapshot shares across
// its columns, so building every column costs one cable scan, one node-uuid map and one channel-macro lookup rather
// than a whole-graph walk per column (docs/architecture/graph-queries.md).
#include "MixerModelInternal.h"

#include "Mixer/MixerSends/MixerSends.h"
#include "Modules/ChannelStripModule.h"
#include <algorithm>

namespace synth {

using NodeID = juce::AudioProcessorGraph::NodeID;

MixerGraphView::MixerGraphView(juce::AudioProcessorGraph& g, const MacroSet& m)
    : graph(g)
    , macros(m)
    , cables(g)
    , links(g)
    , channelMacros(g, m) {}

// isBusStrip's rule, asked of this view's index and reach map (the same answers as its own walks), once per strip.
bool MixerGraphView::isBus(NodeID stripId) const {
    if (const auto known = busByStrip_.find(stripId.uid); known != busByStrip_.end())
        return known->second;
    auto* node = graph.getNodeForId(stripId);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    const bool bus = strip != nullptr && (strip->isBus() || (!findStripsFeedingStrip(graph, cables, stripId).empty() &&
                                                             trackSourcesFeeding(stripId).empty()));
    busByStrip_[stripId.uid] = bus;
    return bus;
}

// busFallbackName's ordinal, with the graph's bus strips listed once per view rather than once per name.
juce::String MixerGraphView::busName(NodeID stripId) const {
    if (!buses_.has_value()) {
        buses_.emplace();
        for (auto* node : graph.getNodes())
            if (node != nullptr && isBus(node->nodeID))
                buses_->push_back(node->nodeID);
        std::sort(buses_->begin(), buses_->end(), [](NodeID a, NodeID b) { return a.uid < b.uid; });
    }
    const auto found = std::find(buses_->begin(), buses_->end(), stripId);
    const int ordinal = found != buses_->end() ? static_cast<int>(std::distance(buses_->begin(), found)) + 1 : 1;
    return "Bus " + juce::String(ordinal);
}

NodeID MixerGraphView::stripFedBy(NodeID trackSourceId) const {
    return links.reach().stripFedByTrackSource(trackSourceId);
}

const std::vector<NodeID>& MixerGraphView::trackSourcesFeeding(NodeID strip) const {
    return links.reach().trackSourcesFeedingStrip(strip);
}

NodeID MixerGraphView::trackSource(const Track& track) const {
    auto* node = links.nodeByUuid(track.bindingUuid);
    return node != nullptr ? node->nodeID : NodeID{};
}

} // namespace synth
