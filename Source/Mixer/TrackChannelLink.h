#pragma once

#include "Mixer/ChannelFlows/ChannelFlows.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>
#include <vector>

// TrackChannelLink.h (docs/mixer/mixer.md#channels-follow-audio-not-tracks): the LINK RULE itself,
// resolved from the live graph plus the TimelineDoc and nothing else.
//
// Core, exactly like Source/Mixer/ChannelFlows: no AppUndoManager, no GraphEditor, no UI. The
// answer is a pure query recomputed on demand rather than a cached flag -- the link forms and
// breaks as the user patches cables, so anything cached would be one graph edit away from lying.
// The UI-side fan-out (name/colour/mute/solo, the channel chip) lives in
// Source/UI/Timeline/TrackChannelLinkController.

namespace synth {

/** What docs/mixer/mixer.md#channels-follow-audio-not-tracks's link rule says about one track, right now. */
struct TrackChannelLinkInfo {
    /** This track's bound source node reaches SOME ChannelStripModule -- the channel chip's
     *  visibility rule ("linked or not"). False for an unbound/orphaned track, an Automation
     *  track, or a chain whose audio has never reached a channel. */
    bool hasChannel = false;

    /** The strip reached, valid only while `hasChannel`. */
    juce::AudioProcessorGraph::NodeID stripId;
    juce::String stripUuid;

    /** `hasChannel` AND this track is that channel's ONLY source --
     * docs/mixer/mixer.md#channels-follow-audio-not-tracks's link rule. */
    bool linked = false;

    /** Every track-source node feeding `stripId` (this track's own included). Size 1 is exactly
     *  `linked`; larger is the shared-channel case (Kick/Snare/Hats into one sampler). */
    std::vector<juce::AudioProcessorGraph::NodeID> feedingTrackSources;
};

/** Resolves `track`'s binding to a live node, walks forward to its channel strip
 *  (findStripFedByTrackSource) and back to that strip's own feeders
 *  (findTrackSourcesFeedingStrip) to decide linked vs shared. Pure: no mutation, no undo, safe to
 *  call on every refresh and on every click. An empty/unresolvable binding yields a default
 *  (hasChannel false) result. */
TrackChannelLinkInfo resolveTrackChannelLink(juce::AudioProcessorGraph& graph, const TimelineDoc& doc, TrackId track);

/** The display name for the strip at `stripId` derived from the tracks that feed it: exactly one
 *  upstream track source resolving to a non-empty track name wins; zero, several, or an
 *  unresolvable source all fall back to `fallback`.
 *
 *  This is the stem-naming rule (StemSession passes "Channel N") shared with the channel chip.
 *  The chip prefers the channel MACRO's name when the strip is boxed
 * (docs/mixer/mixer.md#channels-follow-audio-not-tracks: a channel's name IS its macro's name) and only falls through
 * to here -- macros are a UI concept Core has no access to, so that preference is applied by the caller, not here. */
juce::String channelDisplayName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId,
                                const TimelineDoc& doc, const juce::String& fallback);

/** The same two answers as resolveTrackChannelLink / channelDisplayName, for MANY tracks of one graph state: a
 *  node-uuid index plus a TrackChannelReachMap built once, so a timeline rebuild costs one graph scan rather than a
 *  graph walk per track header. Results are identical to the free functions for every track. A SNAPSHOT: build one
 *  per rebuild and drop it before the graph can change. */
class TrackChannelLinkMap {
public:
    explicit TrackChannelLinkMap(juce::AudioProcessorGraph& graph);

    TrackChannelLinkInfo resolve(const TimelineDoc& doc, TrackId track) const;
    juce::String displayName(juce::AudioProcessorGraph::NodeID stripId, const TimelineDoc& doc,
                             const juce::String& fallback) const;

private:
    juce::AudioProcessorGraph& graph_;
    TrackChannelReachMap reach_;
    std::map<juce::String, juce::AudioProcessorGraph::Node*> nodesByUuid_; // first node per uuid, as the free walk
};

} // namespace synth
