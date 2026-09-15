#include "TrackChannelLink.h"

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include <algorithm>

namespace synth {

namespace {

// The message-thread-canonical uuid (ChannelFlows' own `inMacro` reads the same property for the
// same reason) - never ModuleBase::getNodeUuid(), which is the audio-thread-safe mirror and returns
// a raw const char* rather than a juce::String.
juce::String nodeUuidOf(juce::AudioProcessorGraph::Node* node) {
    return node != nullptr ? node->properties["uuid"].toString() : juce::String();
}

// Core cannot call MainComponent::findNodeByUuid, so the same one-liner lives here.
juce::AudioProcessorGraph::Node* findNodeByUuid(juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    if (uuid.isEmpty())
        return nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && nodeUuidOf(node) == uuid)
            return node;
    return nullptr;
}

// `doc`'s track whose bindingUuid equals `nodeUuid`, or an empty string when there is no uuid or no
// such track (an unbound/untracked node - e.g. a graph built directly by a test).
juce::String trackNameForNodeUuid(const TimelineDoc& doc, const juce::String& nodeUuid) {
    if (nodeUuid.isEmpty())
        return {};
    for (const auto& track : doc.getTracks())
        if (track.bindingUuid == nodeUuid)
            return track.name;
    return {};
}

} // namespace

TrackChannelLinkInfo resolveTrackChannelLink(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, TrackId track) {
    TrackChannelLinkInfo info;

    const auto* t = doc.getTrack(track);
    if (t == nullptr || t->kind == TrackKind::Automation)
        return info; // an Automation track hosts lanes; it plays into no channel at all

    auto* sourceNode = findNodeByUuid(graph, t->bindingUuid);
    if (sourceNode == nullptr)
        return info; // unbound, or orphaned (the bound node is gone)

    const auto stripId = findStripFedByTrackSource(graph, sourceNode->nodeID);
    auto* stripNode = graph.getNodeForId(stripId);
    if (stripNode == nullptr)
        return info; // this track's audio has never reached a channel

    info.hasChannel = true;
    info.stripId = stripId;
    info.stripUuid = nodeUuidOf(stripNode);
    info.feedingTrackSources = findTrackSourcesFeedingStrip(graph, stripId);
    // "The track is the channel's ONLY source" (§5.2). The one feeder must be this track's own
    // node: a strip fed by exactly one track source that is somebody ELSE's is not this track's
    // link (it cannot be reached from here in practice, but asserting it keeps the rule literal).
    info.linked = info.feedingTrackSources.size() == 1 && info.feedingTrackSources.front() == sourceNode->nodeID;
    return info;
}

juce::String channelDisplayName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId,
                                const TimelineDoc& doc, const juce::String& fallback) {
    const auto tracks = findTrackSourcesFeedingStrip(graph, stripId);
    if (tracks.size() != 1)
        return fallback;

    const juce::String trackName = trackNameForNodeUuid(doc, nodeUuidOf(graph.getNodeForId(tracks.front())));
    return trackName.isNotEmpty() ? trackName : fallback;
}

} // namespace synth
