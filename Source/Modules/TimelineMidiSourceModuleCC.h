#pragma once

#include "../Timeline/AutomationKernel.h"
#include "../Timeline/TimelineSnapshot.h"
#include <array>
#include <cmath>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

/**
 * @brief The CC half of "Track In" (TimelineMidiSourceModule): plays a MIDI track's per-clip CC
 * lanes (TimelineSnapshot::ControllerInfo) into the same MIDI buffer the notes go to, so a CC
 * reaches exactly the track's MIDI destinations. See docs/timeline/piano-roll-lanes.md#playback.
 *
 * Audio thread only. No allocation, no locks, no logging: the only state is a fixed 16 x 128 table
 * of the last 7-bit value sent per (channel, controller), -1 meaning "nothing sent / unknown".
 *
 * Emission rule: within one beat range, each lane overlapping the range is evaluated (with
 * AutomationKernel, the automation lanes' own evaluator) at the range start (or the clip start,
 * when the clip begins inside the range) and at every breakpoint inside the range, and a CC is
 * emitted only when the rounded value differs from the table. A Linear ramp therefore steps at
 * most once per block plus once per breakpoint — the resolution of the block size, bounded, with
 * no per-sample work. At most kMaxEventsPerRange messages leave per range; anything past that is
 * simply re-derived at the next range start, because the table was not updated for it.
 *
 * Chase: releaseAndForget() is called by every positional hygiene flush (stop, locate, loop wrap,
 * track lost / muted / soloed away, bypass). It first lets go of any switch pedal it holds down
 * (CC 64 / 66 / 69 last sent at >= 64 gets an explicit 0 — the note-offs the flush sends would
 * otherwise be swallowed by a still-held sustain), then forgets the table. The next evaluation
 * therefore RE-SENDS the current value at the new position: starting playback mid-clip, locating
 * or wrapping the loop always lands the controller where the lane says it is.
 *
 * Outside every clip's window nothing is sent: the controller keeps the last value it was given —
 * EXCEPT a held switch pedal (CC 64 / 66 / 69 last sent at >= 64): the moment no lane for that
 * controller on that channel covers the playhead any more (the lane or its clip ended, or the clip
 * was muted, deleted or edited out of the snapshot), releaseUncoveredSwitches sends it an explicit 0
 * at that beat, so a pedal never stays down after the data that pressed it is gone.
 *
 * Cost: the per-track run is sorted by startBeat and carries a monotonic runMaxEndBeat, so each
 * range binary-searches its first live lane — independent of how many lanes the song has behind
 * the playhead.
 */
class TimelineControllerPlayer {
public:
    static constexpr int kMaxEventsPerRange = 512;

    TimelineControllerPlayer() noexcept { forget(); }

    // Forgets every sent value without emitting anything (prepareToPlay).
    void forget() noexcept { lastSent_.fill(-1); }

    // Releases held switch pedals at `offset`, then forgets (see the class comment).
    void releaseAndForget(juce::MidiBuffer& midi, int offset) noexcept {
        for (int channel = 1; channel <= 16; ++channel)
            for (const int cc : {64, 66, 69})
                if (lastSent_[index(channel, cc)] >= 64)
                    midi.addEvent(juce::MidiMessage::controllerEvent(channel, cc, 0), offset);
        forget();
    }

    // Last value sent on (channel 1..16, cc 0..127), or -1. Diagnostics and tests.
    int lastSentValue(int channel, int cc) const noexcept {
        if (channel < 1 || channel > 16 || cc < 0 || cc > 127)
            return -1;
        return lastSent_[index(channel, cc)];
    }

    // Emits the track's CC changes inside [rangeStart, rangeEnd), offsets measured from baseOffset
    // and clamped to [baseOffset, lastSample] exactly like the note path.
    void emitRange(const TimelineSnapshot& snapshot, const TimelineSnapshot::TrackInfo& track, juce::MidiBuffer& midi,
                   double rangeStart, double rangeEnd, int baseOffset, double beatsPerSample, int lastSample) noexcept {
        if (!(rangeEnd > rangeStart) || !(beatsPerSample > 0.0))
            return;
        int budget = kMaxEventsPerRange;
        const int first = firstLiveLane(snapshot, track, rangeStart);
        const int last = track.firstController + track.numControllers;
        for (int i = first; i < last && budget > 0; ++i) {
            const auto& lane = snapshot.controllers[(std::size_t)i];
            if (lane.startBeat >= rangeEnd)
                break; // sorted by startBeat: every later lane starts later still
            if (lane.endBeat <= rangeStart || lane.numPoints <= 0)
                continue;
            const TimelineSnapshot::Point* points = snapshot.points.data() + lane.firstPoint;
            const double windowStart = juce::jmax(rangeStart, lane.startBeat);
            const double windowEnd = juce::jmin(rangeEnd, lane.endBeat);
            AutomationCursor cursor{};
            const auto sendAt = [&](double beat) {
                const double value = AutomationKernel::evaluate(points, lane.numPoints, beat, points[0].value, cursor);
                const int v7 = juce::jlimit(0, 127, (int)std::lround(std::isfinite(value) ? value : 0.0));
                const int offset = beatToOffset(beat, rangeStart, beatsPerSample, baseOffset, lastSample);
                send(midi, lane, v7, offset, budget);
            };
            sendAt(windowStart);
            for (int p = firstPointAfter(points, lane.numPoints, windowStart); p < lane.numPoints && budget > 0; ++p) {
                if (points[p].beat >= windowEnd)
                    break;
                sendAt(points[p].beat);
            }
        }
        releaseUncoveredSwitches(snapshot, track, midi, first, rangeStart, rangeEnd, baseOffset, beatsPerSample,
                                 lastSample);
    }

private:
    static std::size_t index(int channel, int cc) noexcept { return (std::size_t)((channel - 1) * 128 + cc); }

    static constexpr int kSwitchCcs[3] = {64, 66, 69};

    // First entry of the track's run whose runMaxEndBeat > rangeStart: every entry before it ended
    // at or before the range, so none of them can overlap it (runMaxEndBeat is monotonic).
    static int firstLiveLane(const TimelineSnapshot& snapshot, const TimelineSnapshot::TrackInfo& track,
                             double rangeStart) noexcept {
        int lo = track.firstController;
        int hi = track.firstController + track.numControllers;
        while (lo < hi) {
            const int mid = lo + ((hi - lo) >> 1);
            if (snapshot.controllers[(std::size_t)mid].runMaxEndBeat > rangeStart)
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo;
    }

    // The end beat of the lane for (cc, channel) covering `beat`, or -1 when none does. Only
    // entries from `first` can cover a beat at or after the range start; scanning stops at the
    // first entry starting after `beat` (sorted by start).
    static double coveringLaneEnd(const TimelineSnapshot& snapshot, const TimelineSnapshot::TrackInfo& track, int first,
                                  int cc, int channel, double beat) noexcept {
        const int last = track.firstController + track.numControllers;
        double end = -1.0;
        for (int i = first; i < last; ++i) {
            const auto& lane = snapshot.controllers[(std::size_t)i];
            if (lane.startBeat > beat)
                break;
            if (lane.ccNumber == cc && lane.numPoints > 0 && (lane.channelMask & (1u << (channel - 1))) != 0 &&
                lane.endBeat > beat)
                end = juce::jmax(end, lane.endBeat);
        }
        return end;
    }

    // See the class comment. Runs AFTER the range's lanes emit (so a lane ending inside the range
    // cannot re-press behind its own release): follows the chain of covering lanes from the range
    // start (bounded) and releases at the first uncovered beat inside the range. If a lane further
    // on re-pressed the pedal in this same range, the table now says 0 and the next range simply
    // re-sends that lane's value — a duplicate, never a stuck pedal.
    void releaseUncoveredSwitches(const TimelineSnapshot& snapshot, const TimelineSnapshot::TrackInfo& track,
                                  juce::MidiBuffer& midi, int first, double rangeStart, double rangeEnd, int baseOffset,
                                  double beatsPerSample, int lastSample) noexcept {
        for (const int cc : kSwitchCcs) {
            for (int channel = 1; channel <= 16; ++channel) {
                auto& slot = lastSent_[index(channel, cc)];
                if (slot < 64)
                    continue;
                double beat = rangeStart;
                bool release = false;
                for (int hop = 0; hop < 8; ++hop) {
                    const double end = coveringLaneEnd(snapshot, track, first, cc, channel, beat);
                    if (end < 0.0) {
                        release = true;
                        break;
                    }
                    if (end >= rangeEnd)
                        break; // covered for the rest of this range
                    beat = end;
                }
                if (!release)
                    continue;
                midi.addEvent(juce::MidiMessage::controllerEvent(channel, cc, 0),
                              beatToOffset(beat, rangeStart, beatsPerSample, baseOffset, lastSample));
                slot = 0;
            }
        }
    }

    void send(juce::MidiBuffer& midi, const TimelineSnapshot::ControllerInfo& lane, int v7, int offset,
              int& budget) noexcept {
        const int cc = juce::jlimit(0, 127, lane.ccNumber);
        for (int channel = 1; channel <= 16 && budget > 0; ++channel) {
            if ((lane.channelMask & (1u << (channel - 1))) == 0)
                continue;
            auto& slot = lastSent_[index(channel, cc)];
            if (slot == v7)
                continue;
            midi.addEvent(juce::MidiMessage::controllerEvent(channel, cc, v7), offset);
            slot = (std::int8_t)v7;
            --budget;
        }
    }

    // First index whose beat is strictly greater than `beat` (binary search over the sorted run).
    static int firstPointAfter(const TimelineSnapshot::Point* points, int numPoints, double beat) noexcept {
        int lo = 0;
        int hi = numPoints;
        while (lo < hi) {
            const int mid = lo + ((hi - lo) >> 1);
            if (points[mid].beat > beat)
                hi = mid;
            else
                lo = mid + 1;
        }
        return lo;
    }

    // Same mapping and clamps as TimelineMidiSourceModule::beatToOffset.
    static int beatToOffset(double beat, double rangeStart, double beatsPerSample, int baseOffset,
                            int lastSample) noexcept {
        const double rel = juce::jlimit(-1.0e9, 1.0e9, (beat - rangeStart) / beatsPerSample);
        const std::int64_t offset = (std::int64_t)baseOffset + std::llround(rel);
        return (int)juce::jlimit<std::int64_t>(baseOffset, juce::jmax(baseOffset, lastSample), offset);
    }

    std::array<std::int8_t, 16 * 128> lastSent_{};
};

} // namespace synth
