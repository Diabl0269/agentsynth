#pragma once

#include "BounceExporter.h"
#include <juce_core/juce_core.h>

class AudioEngine;

namespace synth {

class TimelineDoc; // Forward declaration (Source/Timeline/TimelineDoc.h) - see exportStems'
                   // `timelineDoc` parameter comment.

// Offline stem export (P9-8, docs/mixer.md §5.12): ONE render pass through the exact same offline
// path BounceExporter uses, writing one audio file per mixer channel strip instead of one file for
// the whole mix. "Single render, parallel writers" — every ChannelStripModule in the graph gets an
// opt-in tap (ChannelStripModule::setStemTapBuffer) armed for the render's duration; at the end of
// every rendered block, each strip's tapped buffer (its FINAL stereo output — post gain, pan, mute
// AND solo, exactly what it hands to Master) is written to that strip's own file.
//
// Every strip ALWAYS gets a file, muted or soloed-out included — the tap sits AFTER the strip's own
// bypass/mute/solo logic, so a muted or non-soloed strip's stem is simply silent for exactly the
// blocks it was silent. This is what keeps "the stems sum back to the pre-Master mix" true in every
// case, solo included (docs/mixer_implementation.md P9-8): a soloed strip during export never changes which
// strips get written, only what most of them contain.
//
// Master's Direct input (cables that bypass every strip) is NOT a stem — Direct is not a channel
// (docs/mixer.md §5.10 "what the mixer shows"), so it contributes to a normal bounce of the same
// range but is absent from the stem set by design; summing the stems reproduces the pre-Master MIX
// bus, not the whole signal Master receives.
struct StemResult {
    bool ok = false;
    juce::String message;           // always set: the reason on failure, a one-line summary on success
    juce::int64 samplesWritten = 0; // per stem file - every stem is the same length
    int streamDropouts = 0;         // same meaning as BounceResult::streamDropouts

    // One entry per ChannelStrip node found in the graph, in the same stable order the files were
    // numbered in ("NN - <name>.<ext>"). Empty (with ok == false) when the patch has no strips.
    juce::Array<juce::File> stemFiles;
};

class StemExporter {
public:
    using ProgressCallback = BounceExporter::ProgressCallback;

    // Shown (as StemResult::message on a failed export, and by MainComponent's pre-flight check
    // before it even opens the Export Stems dialog) when the patch has no ChannelStrip nodes.
    static constexpr const char* kNoChannelsMessage = "No mixer channels yet - use Create channels in the mixer first.";

    // True when the graph has at least one ChannelStrip node, i.e. exportStems() below would have at
    // least one file to write. Cheap - a scan of the graph's existing nodes, nothing allocated of
    // its own. MainComponent::promptExportStems calls this first so a patch with no channels gets
    // kNoChannelsMessage immediately, without opening a dialog whose render would fail right away.
    static bool hasChannelStrips(AudioEngine& engine);

    // MESSAGE THREAD, BLOCKING. Renders [startBeat, endBeat] + tail exactly like
    // BounceExporter::bounce() (same options, same suspend/restore choreography, same progress and
    // cancel semantics — return false from `progress` to cancel), but writes one file per
    // ChannelStrip node into `destinationFolder` instead of one file for the whole mix.
    //
    // Atomicity: every stem is written to its own sibling temp file; all of them are moved into
    // place only once every one of them finished writing successfully. A failed or cancelled export
    // leaves no stem files behind and never touches a file that was already in `destinationFolder`.
    //
    // Fails immediately (before creating `destinationFolder` or opening any file) when the graph has
    // no ChannelStrip nodes - there is nothing to export, and rendering N=0 files is not success.
    //
    // `timelineDoc` (FRO55, docs/mixer.md §5.12): the live document each stem's file name is
    // resolved against - null is fine (every stem then falls back to "Channel N"; see
    // StemSession's constructor comment for the full naming rule).
    static StemResult exportStems(AudioEngine& engine, const juce::File& destinationFolder,
                                  const BounceOptions& options, const ProgressCallback& progress = {},
                                  const TimelineDoc* timelineDoc = nullptr);

    StemExporter() = delete;
};

} // namespace synth
