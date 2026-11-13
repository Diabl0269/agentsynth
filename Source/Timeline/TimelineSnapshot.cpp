#include "TimelineSnapshot.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include <algorithm>
#include <cmath>
#include <cstring>

namespace synth {

namespace {

// Copies at most `capacity - 1` bytes and always NUL-terminates. Truncation is by BYTE, not by
// codepoint — the fields this is used for (node uuids, param ids, bundle-relative asset paths) are
// ASCII by construction, and the audio thread only ever strcmps the result against another such
// copy (or hands it to the streamer's message-thread resolver).
void copyFixedString(char* dest, const juce::String& source, std::size_t capacity) noexcept {
    const auto* utf8 = source.toRawUTF8();
    const auto length = std::strlen(utf8);
    const auto copied = std::min<std::size_t>(length, capacity - 1);
    std::memcpy(dest, utf8, copied);
    std::memset(dest + copied, 0, capacity - copied);
}

void copyFixedString(char* dest, const juce::String& source) noexcept {
    copyFixedString(dest, source, (std::size_t)TimelineSnapshot::kMaxStringBytes);
}

bool noteEventLess(const TimelineSnapshot::NoteEvent& a, const TimelineSnapshot::NoteEvent& b) noexcept {
    return a.startBeat < b.startBeat;
}

// dB -> linear, with juce::Decibels' own -100 dB floor (anything at or below it is exactly silent).
// Spelled out here rather than pulled from juce_audio_basics so this translation unit keeps its
// juce_core-only dependency, and so the floor is visible at the one place it is applied.
float gainFromDecibels(double gainDb) noexcept {
    if (!std::isfinite(gainDb) || gainDb <= -100.0)
        return 0.0f;
    return (float)std::pow(10.0, gainDb * 0.05);
}

// The delay swing gives a hit starting at the clip-relative `startBeat`: every ODD step of the clip's grid (steps
// counted from 0) is pushed later by (swing - 0.5) * 2 steps, so 0.75 puts the off-beat of a pair at 75% of it. A hit
// off the grid is untouched, and a shift that would leave the clip is dropped, so a swung hit always starts inside its
// clip.
double swingDelayBeats(double swing, double startBeat, double step, double clipLength) noexcept {
    if (swing <= Clip::kStraightSwing)
        return 0.0;
    const double index = startBeat / step;
    const double nearest = std::round(index);
    if (std::abs(index - nearest) > 1.0e-6 || (static_cast<long long>(nearest) & 1LL) == 0)
        return 0.0;
    const double delay = (swing - Clip::kStraightSwing) * 2.0 * step;
    return startBeat + delay < clipLength ? delay : 0.0;
}

constexpr double kEps = 1.0e-6;
constexpr double kFlamLeadSteps = 0.15; // the grace hit sits this far (in steps) before the hit
constexpr double kHumaniseTimingSteps = 0.08;
constexpr double kHumaniseVelocity = 24.0;

// Everything the expansion of one clip's notes shares.
struct ClipGrid {
    const Clip& clip;
    double bar;
    double step;
    int linkIndex; // this clip's place among the track's clips of its link group
};

const Clip::DrumRow* findDrumRow(const Clip& clip, int pitch) noexcept {
    for (const auto& row : clip.drumRows)
        if (row.pitch == pitch)
            return &row;
    return nullptr;
}

// A uniform value in [-1, 1) from 24 bits of `hash`.
double unitFromHash(std::uint64_t hash, int shift) noexcept {
    return static_cast<double>((hash >> shift) & 0xFFFFFFu) / 8388608.0 - 1.0;
}

// The events of one occurrence of a note: its hit (or `repeat` hits spread over a step) and an optional grace hit.
// `occurrenceStart` is clip-relative and before swing, nudge and humanise.
void appendOccurrence(std::vector<TimelineSnapshot::NoteEvent>& out, const ClipGrid& grid, const MidiNote& note,
                      double occurrenceStart, int repetition, double swing, int cycle, int cyclesPerPass) {
    const Clip& clip = grid.clip;
    const std::uint64_t hash =
        TimelineSnapshot::mixHash(static_cast<std::uint64_t>(note.id.value), static_cast<std::uint64_t>(repetition));
    double start =
        occurrenceStart + swingDelayBeats(swing, occurrenceStart, grid.step, clip.lengthBeats) + note.nudge * grid.step;
    int velocity = note.velocity;
    if (clip.humanise > 0.0) {
        start += unitFromHash(hash, 0) * kHumaniseTimingSteps * clip.humanise * grid.step;
        velocity = std::clamp(
            velocity + static_cast<int>(std::lround(unitFromHash(hash, 24) * kHumaniseVelocity * clip.humanise)), 1,
            127);
    }
    start = std::max(0.0, start);
    if (start >= clip.lengthBeats)
        return;

    const double clipEnd = clip.startBeat + clip.lengthBeats;
    TimelineSnapshot::NoteEvent event;
    event.pitch = note.pitch;
    event.velocity = velocity;
    event.channel = note.channel;
    event.rollSeed = static_cast<std::uint32_t>(hash);
    event.chance = static_cast<std::uint8_t>(note.chance);
    event.condHit = static_cast<std::uint8_t>(note.condHit);
    event.condOf = static_cast<std::uint8_t>(note.condOf);
    event.cycle = cycle + grid.linkIndex * cyclesPerPass;
    event.cyclesPerPass = cyclesPerPass;

    const int repeats = std::max(1, note.repeat);
    const double slot = grid.step / static_cast<double>(repeats);
    const double hitLength = repeats > 1 ? std::min(note.lengthBeats, slot) : note.lengthBeats;
    if (note.flam) {
        const double lead = kFlamLeadSteps * grid.step;
        if (start - lead >= 0.0) {
            auto grace = event;
            grace.velocity = std::max(1, static_cast<int>(std::lround(0.6 * velocity)));
            grace.startBeat = clip.startBeat + start - lead;
            grace.endBeat = std::min(grace.startBeat + std::min(note.lengthBeats, lead), clipEnd);
            out.push_back(grace);
        }
    }
    for (int j = 0; j < repeats; ++j) {
        const double hitStart = start + static_cast<double>(j) * slot;
        if (hitStart >= clip.lengthBeats)
            break;
        event.startBeat = clip.startBeat + hitStart;
        event.endBeat = std::min(event.startBeat + hitLength, clipEnd);
        out.push_back(event);
    }
}

// One note of a clip with a per-drum length shorter than the clip: only the cells inside the length play, and the
// pattern repeats every length across the clip.
void appendRowRepeats(std::vector<TimelineSnapshot::NoteEvent>& out, const ClipGrid& grid, const MidiNote& note,
                      const Clip::DrumRow& row, double swing) {
    const double rowLength = static_cast<double>(row.lengthSteps) * grid.step;
    if (std::floor(note.startBeat / grid.step + kEps) >= static_cast<double>(row.lengthSteps))
        return; // stored past the drum's length: kept in the clip, never played
    const int cyclesPerPass = std::max(1, static_cast<int>(std::ceil(grid.clip.lengthBeats / rowLength - kEps)));
    for (int r = 0;; ++r) {
        const double occurrence = note.startBeat + static_cast<double>(r) * rowLength;
        if (occurrence >= grid.clip.lengthBeats)
            break;
        appendOccurrence(out, grid, note, occurrence, r, swing, r, cyclesPerPass);
    }
}

// Flattens one clip's notes onto the end of `out`, applying the clip's mute, solo, swing and per-hit drum settings
// (see TimelineSnapshot::notePlays). The run is stable-sorted by start afterwards: swing, nudge, repeats, flams and
// repeated drum patterns all move events out of the stored order.
void appendClipNotes(std::vector<TimelineSnapshot::NoteEvent>& out, const Clip& clip, double beatsPerBar,
                     int linkIndex) {
    const auto runStart = out.size();
    const auto& mutedPitches = clip.mutedPitches;
    const auto& soloPitches = clip.soloPitches;
    const double bar = beatsPerBar > 0.0 ? beatsPerBar : 4.0;
    const ClipGrid grid{clip, bar, bar / static_cast<double>(std::max(1, clip.stepsPerBar)), linkIndex};
    const int barCycles = std::max(1, static_cast<int>(std::ceil(clip.lengthBeats / bar - kEps)));

    for (const auto& note : clip.notes) {
        // Notes are sorted by (startBeat, pitch), so the first one at or past the clip
        // end means every remaining note is too.
        if (note.startBeat >= clip.lengthBeats)
            break;
        // A muted note is skipped, never emitted silent: dropping it keeps the run
        // free of events no consumer would know to ignore. `continue`, not `break` --
        // mute is not part of the sort key, so a muted note says nothing about the
        // ones after it.
        if (note.muted)
            continue;
        // Drum-machine mute/solo is applied here and never written onto the note, so
        // un-muting a row restores exactly the notes that were there.
        if (std::binary_search(mutedPitches.begin(), mutedPitches.end(), note.pitch))
            continue;
        if (!soloPitches.empty() && !std::binary_search(soloPitches.begin(), soloPitches.end(), note.pitch))
            continue;

        const auto* row = findDrumRow(clip, note.pitch);
        const double swing = (row != nullptr && row->swing >= 0.0) ? row->swing : clip.swing;
        const double rowLength = row != nullptr ? static_cast<double>(row->lengthSteps) * grid.step : 0.0;
        if (row != nullptr && row->lengthSteps > 0 && rowLength < clip.lengthBeats - kEps)
            appendRowRepeats(out, grid, note, *row, swing);
        else
            appendOccurrence(out, grid, note, note.startBeat, 0, swing,
                             static_cast<int>(std::floor(note.startBeat / bar + kEps)), barCycles);
    }
    std::stable_sort(out.begin() + static_cast<std::ptrdiff_t>(runStart), out.end(), noteEventLess);
}

// The clip's place among the track's clips of the same non-zero link group (in clip order, muted ones counted), so
// linked copies of one pattern continue each other's condition cycle.
int linkIndexOf(const Track& track, std::size_t clipIndex) noexcept {
    const auto group = track.clips[clipIndex].linkGroup;
    if (group == 0)
        return 0;
    int index = 0;
    for (std::size_t i = 0; i < clipIndex; ++i)
        if (track.clips[i].linkGroup == group)
            ++index;
    return index;
}
} // namespace

std::atomic<int>& TimelineSnapshot::liveInstanceCount() noexcept {
    static std::atomic<int> count{0};
    return count;
}

TimelineSnapshot::TimelineSnapshot() { liveInstanceCount().fetch_add(1, std::memory_order_relaxed); }

TimelineSnapshot::~TimelineSnapshot() { liveInstanceCount().fetch_sub(1, std::memory_order_relaxed); }

std::unique_ptr<TimelineSnapshot> TimelineSnapshot::buildFrom(const TimelineDoc& doc, double beatsPerBar,
                                                              const InputStatusProvider& inputStatus) {
    auto snapshot = std::make_unique<TimelineSnapshot>();
    snapshot->revision = doc.getRevision();

    const auto& docTracks = doc.getTracks();
    snapshot->tracks.reserve(docTracks.size());

    for (const auto& track : docTracks) {
        TrackInfo info;
        info.trackId = track.id.value;
        info.kind = static_cast<int>(track.kind);
        info.muted = track.muted;
        info.soloed = track.soloed;
        info.armed = track.armed;
        copyFixedString(info.bindingUuid, track.bindingUuid);
        if (track.kind == TrackKind::Audio) {
            info.monitorMode = static_cast<int>(track.monitorMode);
            if (track.audioInput.kind == AudioInputChoice::Kind::Device) {
                info.inputFirstChannel = track.audioInput.firstChannel;
                info.inputNumChannels = track.audioInput.numChannels;
                info.inputDeviceHash = track.audioInput.deviceName.hashCode();
                if (track.audioInput.inputName.isNotEmpty() && inputStatus) {
                    // Found by name on whatever is open, so the device it was picked on does not matter.
                    const auto resolved = resolveAudioInput(track.audioInput, inputStatus());
                    info.inputFirstChannel = resolved.firstChannel;
                    info.inputNumChannels = resolved.nameMissing ? 0 : resolved.numChannels;
                    info.inputDeviceHash = 0;
                }
            }
        }

        // Audio tracks render now (TimelineAudioSourceModule), so a soloed one is part of the
        // same document-wide predicate a soloed MIDI track is — solo would otherwise mean "solo,
        // except audio", which is not what any DAW's solo button does. Automation tracks stay out:
        // nothing plays them.
        if ((track.kind == TrackKind::Midi || track.kind == TrackKind::Audio) && track.soloed)
            snapshot->anySoloed = true;

        // -- notes: flatten every clip into one sorted run -----------------------
        // MIDI tracks only. One Clip type covers both kinds in the doc, so the track's kind
        // is what decides which half of a clip is flattened; a note left on an audio track is inert
        // rather than half-played, and the same clip's assetRef is inert on a MIDI track.
        info.firstNote = static_cast<int>(snapshot->notes.size());

        if (track.kind == TrackKind::Midi) {
            for (std::size_t clipIndex = 0; clipIndex < track.clips.size(); ++clipIndex) {
                const auto& clip = track.clips[clipIndex];
                // A muted clip is excluded whole, before anything else is looked at — see the
                // flatten policy on TimelineSnapshot.
                if (clip.muted)
                    continue;

                const auto runStart = snapshot->notes.size();
                appendClipNotes(snapshot->notes, clip, beatsPerBar, linkIndexOf(track, clipIndex));

                // Each clip contributes an already-sorted run (its notes are sorted and shifted by
                // one constant), so folding it into the track's run is a stable merge, never a sort.
                // Clips may overlap, which is why concatenation would not be enough.
                const auto trackStart = static_cast<std::size_t>(info.firstNote);
                if (runStart > trackStart && snapshot->notes.size() > runStart)
                    std::inplace_merge(snapshot->notes.begin() + static_cast<std::ptrdiff_t>(trackStart),
                                       snapshot->notes.begin() + static_cast<std::ptrdiff_t>(runStart),
                                       snapshot->notes.end(), noteEventLess);
            }
        }

        info.numNotes = static_cast<int>(snapshot->notes.size()) - info.firstNote;

        // -- audio clips -----------------------------------------------------------
        // Audio tracks only, and no merge step: the doc keeps a track's clips sorted by
        // (startBeat, id) already, so copying them in order yields a run sorted by startBeat.
        // Overlapping clips are legal (they SUM at playback — see TimelineAudioSourceModule), which
        // is exactly why the sort key is the start alone and no clipping/truncation happens here.
        info.firstAudioClip = static_cast<int>(snapshot->audioClips.size());

        if (track.kind == TrackKind::Audio) {
            for (const auto& clip : track.clips) {
                // Same exclusion as the note half: a muted clip gets no AudioClipInfo entry at
                // all, so the streamer never even assigns it a stream, let alone reads its file.
                if (clip.muted)
                    continue;

                AudioClipInfo clipInfo;
                clipInfo.clipId = clip.id.value;
                clipInfo.startBeat = clip.startBeat;
                clipInfo.lengthBeats = clip.lengthBeats;
                // Checked BEFORE copying: silently truncating an over-long assetRef would point
                // the streamer at a different, wrong path rather than merely failing to match, so
                // an over-long one is dropped instead — the clip becomes inert, same as an empty
                // assetRef on a MIDI clip.
                if (clip.assetRef.getNumBytesAsUTF8() < (std::size_t)kMaxAssetRefBytes)
                    copyFixedString(clipInfo.assetRef, clip.assetRef, (std::size_t)kMaxAssetRefBytes);
                clipInfo.gainLinear = gainFromDecibels(clip.gainDb);
                clipInfo.fadeInBeats = clip.fadeInBeats;
                clipInfo.fadeOutBeats = clip.fadeOutBeats;
                clipInfo.sourceStartSeconds = clip.sourceStartSeconds;
                snapshot->audioClips.push_back(clipInfo);
            }
        }

        info.numAudioClips = static_cast<int>(snapshot->audioClips.size()) - info.firstAudioClip;

        // -- lanes ---------------------------------------------------------------
        info.firstLane = static_cast<int>(snapshot->lanes.size());

        for (const auto& lane : track.lanes) {
            LaneInfo laneInfo;
            laneInfo.laneId = lane.id.value;
            copyFixedString(laneInfo.nodeUuid, lane.nodeUuid);
            copyFixedString(laneInfo.paramId, lane.paramId);
            laneInfo.minValue = lane.range.minValue;
            laneInfo.maxValue = lane.range.maxValue;
            laneInfo.defaultValue = lane.range.defaultValue;
            laneInfo.recordMode = lane.recordMode;
            laneInfo.paramIndexHint = lane.paramIndexHint;

            laneInfo.firstPoint = static_cast<int>(snapshot->points.size());
            for (const auto& breakpoint : lane.points) {
                Point point;
                point.beat = breakpoint.beat;
                point.value = breakpoint.value;
                point.tension = breakpoint.tension;
                point.curve = breakpoint.curve;
                snapshot->points.push_back(point);
            }
            laneInfo.numPoints = static_cast<int>(snapshot->points.size()) - laneInfo.firstPoint;

            snapshot->lanes.push_back(laneInfo);
        }

        info.numLanes = static_cast<int>(snapshot->lanes.size()) - info.firstLane;

        snapshot->tracks.push_back(info);
    }

    // Solo follows the input: a soloed Audio track recording "Another track" solos its source too, or the source's
    // clips would be suppressed and the take recorded silence (docs/timeline/tracks.md#the-input-row).
    for (const auto& track : docTracks) {
        if (track.kind != TrackKind::Audio || !track.soloed || track.audioInput.kind != AudioInputChoice::Kind::Track)
            continue;
        for (auto& info : snapshot->tracks)
            if (info.trackId == track.audioInput.sourceTrack.value)
                info.soloed = true;
    }

    return snapshot;
}

bool TimelineSnapshot::selfCheck() const noexcept {
    const auto noteCount = static_cast<int>(notes.size());
    const auto laneCount = static_cast<int>(lanes.size());
    const auto pointCount = static_cast<int>(points.size());
    const auto audioClipCount = static_cast<int>(audioClips.size());

    for (const auto& track : tracks) {
        if (track.firstNote < 0 || track.numNotes < 0 || track.firstNote + track.numNotes > noteCount)
            return false;
        if (track.firstLane < 0 || track.numLanes < 0 || track.firstLane + track.numLanes > laneCount)
            return false;
        if (track.firstAudioClip < 0 || track.numAudioClips < 0 ||
            track.firstAudioClip + track.numAudioClips > audioClipCount)
            return false;
        if (track.bindingUuid[kMaxStringBytes - 1] != '\0')
            return false;

        for (int i = track.firstNote + 1; i < track.firstNote + track.numNotes; ++i)
            if (notes[static_cast<std::size_t>(i)].startBeat < notes[static_cast<std::size_t>(i - 1)].startBeat)
                return false;

        for (int i = track.firstAudioClip; i < track.firstAudioClip + track.numAudioClips; ++i) {
            const auto& clip = audioClips[static_cast<std::size_t>(i)];
            if (clip.assetRef[kMaxAssetRefBytes - 1] != '\0')
                return false;
            if (i > track.firstAudioClip && clip.startBeat < audioClips[static_cast<std::size_t>(i - 1)].startBeat)
                return false;
        }
    }

    for (const auto& lane : lanes) {
        if (lane.firstPoint < 0 || lane.numPoints < 0 || lane.firstPoint + lane.numPoints > pointCount)
            return false;
        if (lane.nodeUuid[kMaxStringBytes - 1] != '\0' || lane.paramId[kMaxStringBytes - 1] != '\0')
            return false;

        for (int i = lane.firstPoint + 1; i < lane.firstPoint + lane.numPoints; ++i)
            if (points[static_cast<std::size_t>(i)].beat < points[static_cast<std::size_t>(i - 1)].beat)
                return false;
    }

    return true;
}

} // namespace synth
