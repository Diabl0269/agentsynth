// Concern: (docs/mixer/sends-and-buses.md) -- the bus/send half of buildMixerSnapshot: which
// strips are buses, what a bus column calls itself and lists as its sources, and each column's
// active send rows. Its own unit rather than more lines in MixerModelColumns.cpp (root CLAUDE.md's
// one-concern-per-unit rule).

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Mixer/ChannelMacroLookup.h"
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
    if (const auto* macro = nearestChannelMacro(graph, macros, uuid))
        return macro->name;
    // A strip's own persisted name comes right after the macro (a boxed strip's name IS its
    // macro's -- see the mixer header's inline-rename comment on why there are never two competing
    // names for one column) and ahead of the bus/track-walk fallback below. Empty (unset) falls
    // straight through (see docs/mixer/panel.md).
    if (auto* strip = dynamic_cast<ChannelStripModule*>(node->getProcessor());
        strip != nullptr && strip->getStripName().isNotEmpty())
        return strip->getStripName();
    if (isBusStrip(graph, stripId))
        return busFallbackName(graph, stripId); // a bus has no feeding track to name it
    return channelDisplayName(graph, stripId, doc, "Channel");
}

void buildBusSourcesForColumn(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                              MixerColumn& column) {
    if (column.kind != MixerColumn::Kind::Bus && column.kind != MixerColumn::Kind::Strip)
        return;
    auto& names = column.kind == MixerColumn::Kind::Bus ? column.busSources : column.receivesFrom;
    for (const auto sourceId : findStripsFeedingStrip(graph, column.nodeId))
        names.push_back(stripColumnName(graph, doc, macros, sourceId));
}

namespace {

/** A row's target text: the column name a strip target shows, or "Key: Compressor 1 on
 *  <that column name>" for a Key target -- the column name, not sendTargetName's doc-less one, so
 *  the row reads the same channel name its column header does. */
juce::String sendEntryTargetName(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, const MacroSet& macros,
                                 const SendTarget& target) {
    if (!target.isValid())
        return "No target";
    if (!target.key)
        return stripColumnName(graph, doc, macros, target.node);
    const auto channel = findKeyTargetChannel(graph, target.node);
    return keySendTargetName(graph, target.node,
                             channel != NodeID{} ? stripColumnName(graph, doc, macros, channel) : juce::String());
}

} // namespace

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
        entry.muted = strip->isSendMuted(slot);
        entry.bypassed = strip->isSendBypassed(slot);
        entry.mono = strip->isSendMono(slot);
        const auto target = resolveSendTarget(graph, column.nodeId, slot);
        entry.targetNodeId = target.node;
        entry.keyTarget = target.key;
        entry.targetName = sendEntryTargetName(graph, doc, macros, target);
        column.sends.push_back(std::move(entry));
    }
}

} // namespace synth
