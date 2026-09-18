#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "UI/Chrome/ColourPickerPopup.h"
#include <memory>

// TrackChannelLinkSurface.h -- FRO14 (P9-4, docs/mixer/mixer.md#channels-follow-audio-not-tracks): everything a track header needs
// from the app about the CHANNEL its track plays into.
//
// A narrow seam, deliberately separate from TrackHeaderHost: that interface is about a track's own
// Track In node (bind, create, delete), this one is about the mixer channel downstream of it.
// TrackHeaderHost exposes it through ONE non-pure accessor (getChannelLinkSurface(), null by
// default) so every existing implementer -- test stubs included -- keeps compiling unchanged and
// the God-object header does not grow a method per feature. The real implementer is
// TrackChannelLinkController, owned by MainComponent (the only object holding the doc, the graph,
// the macros and the undo manager at once); the header stays graph-free and headless-testable
// against a stub, exactly as it already is for TrackHeaderHost.

namespace synth::ui {

struct TrackChannelLinkSurface {
    virtual ~TrackChannelLinkSurface() = default;

    /** What the header draws. Recomputed from the live graph on demand -- never cached across a
     *  click, since one cable drag can form or break a link. */
    struct ChannelInfo {
        /** This track's audio/notes reach SOME channel strip. The channel chip's visibility rule:
         *  docs/mixer/mixer.md#channels-follow-audio-not-tracks shows a chip for every such track, linked or not. */
        bool hasChannel = false;
        /** ...and this track is that channel's ONLY source (docs/mixer/mixer.md#channels-follow-audio-not-tracks's link rule). */
        bool linked = false;
        /** The chip's label: the channel macro's name when the strip is boxed, else the one feeding
         *  track's name, else "Channel". Empty when !hasChannel. */
        juce::String channelName;
        /** The strip's OWN mute/solo. For a LINKED track these are what the header's M/S buttons
         *  show and drive (docs/mixer/mixer.md#channels-follow-audio-not-tracks (c)); for a shared channel the header ignores them and keeps
         *  showing the doc's own note-gating flags. */
        bool channelMuted = false;
        bool channelSoloed = false;
        /** Loudest of the strip's two output legs, 0..1-ish, for the chip's meter. */
        float meterPeak = 0.0f;
    };

    virtual ChannelInfo getChannelInfo(synth::TrackId track) const = 0;

    /** JUST the chip meter's level, for the shared 15 Hz tick. Deliberately not getChannelInfo():
     *  that re-derives the whole link from the live graph (a node scan plus two BFS walks over a
     *  fresh copy of every connection), which is fine per click and far too much per frame per row.
     *  This answers from the strip id the last getChannelInfo()/reconcile resolved, so the tick
     *  costs a map lookup and two atomic reads. A stale id is safe by construction: it either no
     *  longer resolves (0) or still names the right strip, and refreshFromDoc() re-resolves on every
     *  doc change anyway. Returns 0 when the track reaches no channel. */
    virtual float getChannelMeterPeak(synth::TrackId track) const = 0;

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks (a): renames the track AND its channel macro as ONE undo step. Returns false when the
     *  track is not linked (or has no macro to rename) -- the caller then performs its ordinary
     *  track-only rename, byte-for-byte as before. */
    virtual bool renameLinkedTrackAndChannel(synth::TrackId track, const juce::String& newName) = 0;

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks (b): a colour picker whose live preview writes the track colour AND the channel macro's
     *  colour on every drag frame with no undo step, whose cancel restores both, and whose commit is
     *  ONE undo step covering both. Null when the track is not linked or its strip is not boxed in a
     *  macro -- the caller falls back to its ordinary single-target picker. `favourites` is the
     *  picker's favourites shelf store (the header already resolves it through
     *  TrackHeaderHost::getAppProperties), passed in rather than re-derived so both pickers persist
     *  to the same place; null for an in-memory-only picker. */
    virtual std::unique_ptr<ColourPickerPopup> buildLinkedChannelColourPicker(synth::TrackId track,
                                                                              juce::PropertiesFile* favourites) = 0;

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks (c): for a LINKED track, toggles the channel strip's own mute parameter / solo flag as
     *  ONE undo step and returns true; the track's doc mute/solo stay false, so there is exactly one
     *  stored mute and one stored solo per linked track. False for an unlinked track: the caller
     *  keeps today's note gating, unchanged. Solo always goes through
     *  AudioEngine::setChannelStripSoloed, never ChannelStripModule::setSoloed (Source/CLAUDE.md). */
    virtual bool toggleLinkedChannelMuted(synth::TrackId track) = 0;
    virtual bool toggleLinkedChannelSoloed(synth::TrackId track) = 0;

    /** docs/mixer/mixer.md#channels-follow-audio-not-tracks's channel chip click: scrolls the channel into view and selects it (the Locate Master
     *  contract), so a chip on a MIDI track is how the user finds where its audio went. THE P9-5
     *  HOOK -- when the mixer panel exists this override opens/focuses that column instead, with no
     *  change to the chip or the header. */
    virtual void revealChannelForTrack(synth::TrackId track) = 0;
};

} // namespace synth::ui
