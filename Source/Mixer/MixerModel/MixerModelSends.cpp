// Concern: FRO15 (P9-9, docs/mixer/sends-and-buses.md) -- the bus/send half of buildMixerSnapshot: which
// strips are buses, what a bus column calls itself and lists as its sources, and each column's
// active send rows. Its own unit rather than more lines in MixerModelColumns.cpp (root CLAUDE.md's
// one-concern-per-unit rule).

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/MixerSends/MixerSends.h"
#include "Mixer/TrackChannelLink.h"
#include "MixerModelInternal.h"
#include "Modules/ChannelStripModule.h"
#include <algorithm>

namespace synth {

namespace {
using NodeID = juce::AudioProcessorGraph::NodeID;
} // namespace

juce::String stripColumnName(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                             NodeID stripId) {
    auto* node = graph.getNodeForId(stripId);
    if (node == nullptr)
        return {};
    const juce::String uuid = node->properties["uuid"].toString();
    if (const auto* macro = macros.findByMember(uuid))
        return macro->name;
    if (isBusStrip(graph, stripId))
        return busFallbackName(graph, stripId); // a bus has no feeding track to name it
    return channelDisplayName(graph, stripId, doc, "Channel");
}

void buildBusSourcesForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                              MixerColumn& column) {
    if (column.kind != MixerColumn::Kind::Bus)
        return;
    for (const auto sourceId : findStripsFeedingStrip(graph, column.nodeId))
        column.busSources.push_back(stripColumnName(graph, doc, macros, sourceId));
}

void buildSendsForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                         MixerColumn& column) {
    auto* node = graph.getNodeForId(column.nodeId);
    auto* strip = node != nullptr ? dynamic_cast<ChannelStripModule*>(node->getProcessor()) : nullptr;
    if (strip == nullptr)
        return;

    for (int slot = 0; slot < ChannelStripModule::kMaxSends; ++slot) {
        if (!strip->isSendActive(slot))
            continue;
        MixerSendEntry entry;
        entry.slot = slot;
        entry.preFader = strip->isSendPreFader(slot);
        entry.targetNodeId = findSendTarget(graph, column.nodeId, slot);
        entry.targetName = entry.targetNodeId == NodeID{} ? juce::String("No target")
                                                          : stripColumnName(graph, doc, macros, entry.targetNodeId);
        column.sends.push_back(std::move(entry));
    }
}

} // namespace synth
