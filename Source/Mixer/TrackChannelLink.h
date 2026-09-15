#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <vector>

// TrackChannelLink.h -- FRO14 (P9-4, docs/mixer.md §5.2): the LINK RULE itself, resolved from the
// live graph plus the TimelineDoc and nothing else.
//
// Core, exactly like Source/Mixer/ChannelFlows: no AppUndoManager, no GraphEditor, no UI. The
// answer is a pure query recomputed on demand rather than a cached flag -- the link forms and
// breaks as the user patches cables, so anything cached would be one graph edit away from lying.
// The UI-side fan-out (name/colour/mute/solo, the channel chip) lives in
// Source/UI/Timeline/TrackChannelLinkController.

namespace synth {

/** What §5.2's link rule says about one track, right now. */
struct TrackChannelLinkInfo {
    /** This track's bound source node reaches SOME ChannelStripModule -- the channel chip's
     *  visibility rule ("linked or not"). False for an unbound/orphaned track, an Automation
     *  track, or a chain whose audio has never reached a channel. */
    bool hasChannel = false;

    /** The strip reached, valid only while `hasChannel`. */
    juce::AudioProcessorGraph::NodeID stripId;
    juce::String stripUuid;

    /** `hasChannel` AND this track is that channel's ONLY source -- §5.2's link rule. */
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
 *  This is FRO55's stem-naming rule (StemSession passes "Channel N") shared with the channel chip.
 *  The chip prefers the channel MACRO's name when the strip is boxed (docs/mixer.md §5.2: a
 *  channel's name IS its macro's name) and only falls through to here -- macros are a UI concept
 *  Core has no access to, so that preference is applied by the caller, not here. */
juce::String channelDisplayName(juce::AudioProcessorGraph& graph, juce::AudioProcessorGraph::NodeID stripId,
                                const TimelineDoc& doc, const juce::String& fallback);

} // namespace synth
