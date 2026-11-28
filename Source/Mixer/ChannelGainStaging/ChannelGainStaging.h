#pragma once

// Gain staging for the channels an AI build adds: a model sets module levels by feel, so a few channels come out far
// over full scale while others sit 20 dB lower. After a batch that added channel strips, the app renders a few short
// windows of the song through the same offline stem render the Export Stems command uses, reads each new channel's
// peak, and lowers that channel's fader just enough to put the peak at the target. Only faders move, only down, only
// on channels the batch created; the fader is linear, so one measurement is enough.
// docs/ai/gain-staging.md

#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <set>
#include <vector>

class AudioEngine;

namespace synth::gainstaging {

using NodeID = juce::AudioProcessorGraph::NodeID;

/** The loudest a channel's peak may be after the pass, in dBFS. */
constexpr double kTargetPeakDb = -3.0;

/** A trim aims this far under the target: two renders of the same song differ by a few tenths of a dB, so a channel
 *  trimmed to exactly the target could still read over it on the next render. */
constexpr double kTrimMarginDb = 0.5;

/** A stretch of the song, in beats, that gets rendered. */
struct BeatWindow {
    double startBeat = 0.0;
    double endBeat = 0.0;
    bool operator==(const BeatWindow& other) const noexcept {
        return startBeat == other.startBeat && endBeat == other.endBeat;
    }
};

struct WindowOptions {
    /** Used only when the whole arrangement is longer than `maxTotalBeats`: the stretch of each track that is measured
     *  (its busiest ones). */
    double windowBeats = 4.0;
    /** The busiest stretches measured per track; the second catches a different part of the song. */
    int windowsPerTrack = 4;
    /** How far before a stretch the render starts, at most, so notes already ringing there are heard. */
    double maxLeadBeats = 8.0;
    /** Windows closer than this are rendered as one. */
    double mergeGapBeats = 2.0;
    /** The most beats rendered in all. An arrangement up to this long is rendered whole, in one window; a longer one
     *  falls back to the busiest stretches, the lower-ranked ones dropped first. */
    double maxTotalBeats = 640.0;
};

/** Where to measure: the whole arrangement (beat 0 to the last note's end) as one window when it is at most
 *  `maxTotalBeats` long, otherwise for each melodic track, the stretches of `windowBeats` where it plays most (a note
 * counts its velocity times the beats it sounds inside the stretch), each started early enough to include the notes
 * that sound in it. Sorted, merged where they touch, empty when no track has notes. Pure: reads only the doc. */
std::vector<BeatWindow> planWindows(const TimelineDoc& doc, const WindowOptions& options = {});

/** One channel's loudest sample over the rendered windows. */
struct ChannelPeak {
    NodeID strip;
    double peakDb = 0.0; // -infinity when the channel stayed silent
};

/** Renders `windows` of the live project through StemExporter (message thread, blocking, the Export Stems path: the
 *  device callback is suspended and put back) and returns every channel strip's loudest sample, in strip order. Empty
 *  when there is nothing to render or the render failed. */
std::vector<ChannelPeak> measureChannelPeaks(AudioEngine& engine, const std::vector<BeatWindow>& windows);

/** One fader move. */
struct Trim {
    NodeID strip;
    double peakDb = 0.0;
    double fromDb = 0.0;
    double toDb = 0.0;
};

/** The fader moves that bring every measured channel in `only` down to `targetPeakDb`: a channel whose peak is over the
 *  target gets its current fader (`currentGainDb`) lowered until the peak sits `kTrimMarginDb` under the target,
 * rounded down to the fader's 0.1 dB step and floored at the fader's minimum. A channel at or under the target, or
 * silent, is left out: nothing is raised. */
std::vector<Trim> planTrims(const std::vector<ChannelPeak>& peaks, const std::set<NodeID>& only,
                            const std::function<double(NodeID)>& currentGainDb, double targetPeakDb = kTargetPeakDb);

/** Every Channel Strip node of `graph`. */
std::set<NodeID> channelStripIds(const juce::AudioProcessorGraph& graph);

/** The whole pass, without the write: plans windows from `doc`, renders them, and returns the fader moves for the
 *  channels in `only` (read from the live strips). Empty when nothing needs to move. */
std::vector<Trim> planChannelTrims(AudioEngine& engine, const TimelineDoc& doc, const std::set<NodeID>& only,
                                   double targetPeakDb = kTargetPeakDb, const WindowOptions& options = {});

} // namespace synth::gainstaging
