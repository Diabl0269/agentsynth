#include "TimelineSnapshot.h"
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

} // namespace

std::atomic<int>& TimelineSnapshot::liveInstanceCount() noexcept {
    static std::atomic<int> count{0};
    return count;
}

TimelineSnapshot::TimelineSnapshot() { liveInstanceCount().fetch_add(1, std::memory_order_relaxed); }

TimelineSnapshot::~TimelineSnapshot() { liveInstanceCount().fetch_sub(1, std::memory_order_relaxed); }

std::unique_ptr<TimelineSnapshot> TimelineSnapshot::buildFrom(const TimelineDoc& doc) {
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

        // TL6-4: audio tracks render now (TimelineAudioSourceModule), so a soloed one is part of the
        // same document-wide predicate a soloed MIDI track is — solo would otherwise mean "solo,
        // except audio", which is not what any DAW's solo button does. Automation tracks stay out:
        // nothing plays them.
        if ((track.kind == TrackKind::Midi || track.kind == TrackKind::Audio) && track.soloed)
            snapshot->anySoloed = true;

        // -- notes: flatten every clip into one sorted run -----------------------
        // MIDI tracks only (TL6-4). One Clip type covers both kinds in the doc, so the track's kind
        // is what decides which half of a clip is flattened; a note left on an audio track is inert
        // rather than half-played, and the same clip's assetRef is inert on a MIDI track.
        info.firstNote = static_cast<int>(snapshot->notes.size());

        if (track.kind == TrackKind::Midi) {
            for (const auto& clip : track.clips) {
                const double clipEnd = clip.startBeat + clip.lengthBeats;
                const auto runStart = snapshot->notes.size();

                for (const auto& note : clip.notes) {
                    // Notes are sorted by (startBeat, pitch), so the first one at or past the clip
                    // end means every remaining note is too.
                    if (note.startBeat >= clip.lengthBeats)
                        break;

                    NoteEvent event;
                    event.startBeat = clip.startBeat + note.startBeat;
                    event.endBeat = std::min(event.startBeat + note.lengthBeats, clipEnd);
                    event.pitch = note.pitch;
                    event.velocity = note.velocity;
                    event.channel = note.channel;
                    snapshot->notes.push_back(event);
                }

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

        // -- audio clips (TL6-4) --------------------------------------------------
        // Audio tracks only, and no merge step: the doc keeps a track's clips sorted by
        // (startBeat, id) already, so copying them in order yields a run sorted by startBeat.
        // Overlapping clips are legal (they SUM at playback — see TimelineAudioSourceModule), which
        // is exactly why the sort key is the start alone and no clipping/truncation happens here.
        info.firstAudioClip = static_cast<int>(snapshot->audioClips.size());

        if (track.kind == TrackKind::Audio) {
            for (const auto& clip : track.clips) {
                AudioClipInfo clipInfo;
                clipInfo.clipId = clip.id.value;
                clipInfo.startBeat = clip.startBeat;
                clipInfo.lengthBeats = clip.lengthBeats;
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
