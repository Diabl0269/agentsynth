#include "TimelineSnapshot.h"
#include <algorithm>
#include <cstring>

namespace synth {

namespace {

// Copies at most kMaxStringBytes - 1 bytes and always NUL-terminates. Truncation is by BYTE, not
// by codepoint — the fields this is used for (node uuids, param ids) are ASCII by construction, and
// the audio thread only ever strcmps the result against another such copy.
void copyFixedString(char* dest, const juce::String& source) noexcept {
    const auto* utf8 = source.toRawUTF8();
    const auto length = std::strlen(utf8);
    const auto copied = std::min<std::size_t>(length, TimelineSnapshot::kMaxStringBytes - 1);
    std::memcpy(dest, utf8, copied);
    std::memset(dest + copied, 0, TimelineSnapshot::kMaxStringBytes - copied);
}

bool noteEventLess(const TimelineSnapshot::NoteEvent& a, const TimelineSnapshot::NoteEvent& b) noexcept {
    return a.startBeat < b.startBeat;
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

        if (track.kind == TrackKind::Midi && track.soloed)
            snapshot->anySoloed = true;

        // -- notes: flatten every clip into one sorted run -----------------------
        info.firstNote = static_cast<int>(snapshot->notes.size());

        for (const auto& clip : track.clips) {
            const double clipEnd = clip.startBeat + clip.lengthBeats;
            const auto runStart = snapshot->notes.size();

            for (const auto& note : clip.notes) {
                // Notes are sorted by (startBeat, pitch), so the first one at or past the clip end
                // means every remaining note is too.
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

            // Each clip contributes an already-sorted run (its notes are sorted and shifted by one
            // constant), so folding it into the track's run is a stable merge, never a sort. Clips
            // may overlap, which is why concatenation would not be enough.
            const auto trackStart = static_cast<std::size_t>(info.firstNote);
            if (runStart > trackStart && snapshot->notes.size() > runStart)
                std::inplace_merge(snapshot->notes.begin() + static_cast<std::ptrdiff_t>(trackStart),
                                   snapshot->notes.begin() + static_cast<std::ptrdiff_t>(runStart),
                                   snapshot->notes.end(), noteEventLess);
        }

        info.numNotes = static_cast<int>(snapshot->notes.size()) - info.firstNote;

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

    for (const auto& track : tracks) {
        if (track.firstNote < 0 || track.numNotes < 0 || track.firstNote + track.numNotes > noteCount)
            return false;
        if (track.firstLane < 0 || track.numLanes < 0 || track.firstLane + track.numLanes > laneCount)
            return false;
        if (track.bindingUuid[kMaxStringBytes - 1] != '\0')
            return false;

        for (int i = track.firstNote + 1; i < track.firstNote + track.numNotes; ++i)
            if (notes[static_cast<std::size_t>(i)].startBeat < notes[static_cast<std::size_t>(i - 1)].startBeat)
                return false;
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
