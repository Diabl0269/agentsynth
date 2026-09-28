// Concern: range edits — the Range tool's composed operations (tempo-aware split, edge trims,
// splitting a track span at both range edges, deleting a range with or without closing the gap,
// and the detached clipped copy a range copy captures).
//
// Every entry point takes `secondsPerBeat` from the caller because this document deliberately has
// no tempo (see the TimelineDoc class comment), yet moving an audio clip's LEFT edge has to move
// where it starts reading its asset — otherwise the audio slides under the clip instead of the
// clip being cut out of it. That is the one thing splitClip deliberately does not do, and the
// reason these edits are not simply compositions of it.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>

namespace synth {

using namespace detail;

namespace {
// A range is a finite, non-empty, non-negative beat span.
bool isValidBeatRange(double startBeat, double endBeat) noexcept {
    return isFiniteAtOrAfterZero(startBeat) && std::isfinite(endBeat) && endBeat > startBeat;
}

// A note cut by a trim keeps its own id on the piece that survives (the other piece is discarded).
NoteId keepOriginalId(const MidiNote& original) noexcept { return original.id; }
} // namespace

// splitClip with a tempo: the right half of an audio clip advances its sourceStartSeconds by the
// cut offset, so it keeps playing the audio that was under it before the cut.
std::pair<ClipId, ClipId> TimelineDoc::splitClipAtTempo(ClipId id, double atBeat, double secondsPerBeat) {
    if (!isFinitePositive(secondsPerBeat))
        return {};
    return splitClipImpl(id, atBeat, secondsPerBeat);
}

// Shrinks the clip from the left. Notes before the new edge are dropped, a straddling note is
// truncated (keeping its id), the rest are re-based so the clip's content stays where it was on the
// timeline. The fade-in is zeroed: the new edge is a cut, exactly like the right half of a split.
// Shrink-only on purpose — growing a clip leftwards would need a negative sourceStartSeconds for
// an audio clip recorded from its first frame.
bool TimelineDoc::trimClipStart(ClipId id, double newStartBeat, double secondsPerBeat) {
    if (!std::isfinite(newStartBeat) || !isFinitePositive(secondsPerBeat))
        return false;
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return false;
    const double end = clip->startBeat + clip->lengthBeats;
    if (!(newStartBeat > clip->startBeat && newStartBeat < end))
        return false;

    return applyMutation([&] {
        const double trimmed = newStartBeat - clip->startBeat;
        std::vector<MidiNote> dropped;
        std::vector<MidiNote> kept;
        partitionNotesAt(clip->notes, trimmed, keepOriginalId, dropped, kept);

        // Lift and re-insert, as moveClip does: the start participates in the clip ordering.
        const auto index = clip - owner->clips.data();
        Clip moved = std::move(*clip);
        moved.notes = std::move(kept);
        moved.startBeat = newStartBeat;
        moved.lengthBeats = end - newStartBeat;
        moved.fadeInBeats = 0.0;
        if (moved.assetRef.isNotEmpty())
            moved.sourceStartSeconds += trimmed * secondsPerBeat;
        owner->clips.erase(owner->clips.begin() + index);
        const auto pos = std::lower_bound(owner->clips.begin(), owner->clips.end(), moved, clipLess);
        owner->clips.insert(pos, std::move(moved));
        return true;
    });
}

// Shrinks the clip from the right. Unlike resizeClip (which leaves notes past the end in place,
// merely unheard), a trim CUTS them — a range delete must actually delete what it covered, or a
// later resize would bring it back. The fade-out is zeroed: the new edge is a cut.
bool TimelineDoc::trimClipEnd(ClipId id, double newEndBeat) {
    if (!std::isfinite(newEndBeat))
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (!(newEndBeat > clip->startBeat && newEndBeat < clip->startBeat + clip->lengthBeats))
        return false;

    return applyMutation([&] {
        const double length = newEndBeat - clip->startBeat;
        std::vector<MidiNote> kept;
        std::vector<MidiNote> dropped;
        partitionNotesAt(clip->notes, length, keepOriginalId, kept, dropped);
        clip->notes = std::move(kept);
        clip->lengthBeats = length; // length doesn't participate in the clip ordering
        clip->fadeOutBeats = 0.0;
        return true;
    });
}

// Clip ids are snapshotted per track before anything is split — every split re-seats the track's
// clip vector — and re-resolved afterwards, so the returned ids reflect the final layout.
std::vector<ClipId> TimelineDoc::splitClipsAtRangeEdges(const std::vector<TrackId>& trackIds, double startBeat,
                                                        double endBeat, double secondsPerBeat) {
    std::vector<ClipId> inside;
    if (!isValidBeatRange(startBeat, endBeat) || !isFinitePositive(secondsPerBeat))
        return inside;

    for (const auto trackId : trackIds) {
        const auto* track = findTrack(trackId);
        if (track == nullptr)
            continue;

        std::vector<ClipId> ids;
        for (const auto& clip : track->clips)
            ids.push_back(clip.id);

        for (const auto id : ids) {
            // A split at the left edge leaves `id` on the outside piece and hands the inside
            // piece a new id, which may itself straddle the right edge.
            auto current = id;
            if (const auto* clip = getClip(current); clip != nullptr && clip->startBeat < startBeat)
                if (const auto parts = splitClipAtTempo(current, startBeat - clip->startBeat, secondsPerBeat);
                    parts.second.isValid())
                    current = parts.second;
            if (const auto* clip = getClip(current); clip != nullptr && clip->startBeat < endBeat)
                splitClipAtTempo(current, endBeat - clip->startBeat, secondsPerBeat);
        }

        if (const auto* after = findTrack(trackId))
            for (const auto& clip : after->clips)
                if (clip.startBeat >= startBeat && clip.startBeat + clip.lengthBeats <= endBeat)
                    inside.push_back(clip.id);
    }
    return inside;
}

// Per overlapping clip: wholly inside -> removed; straddling one edge -> trimmed back to it;
// straddling both -> split at the right edge (tempo-aware, so the right piece keeps its audio) and
// the left piece trimmed. With `closeGap`, every clip on those tracks at or after the range end
// then moves left by the range length — ONLY on the range's own tracks, and only clips: automation
// breakpoints and markers are left where they are (a range covers clip lanes, not the timebase).
bool TimelineDoc::deleteRange(const std::vector<TrackId>& trackIds, double startBeat, double endBeat, bool closeGap,
                              double secondsPerBeat) {
    if (!isValidBeatRange(startBeat, endBeat) || !isFinitePositive(secondsPerBeat))
        return false;

    bool changed = false;
    for (const auto trackId : trackIds) {
        const auto* track = findTrack(trackId);
        if (track == nullptr)
            continue;

        // Snapshot first: every edit below re-seats this track's clip vector.
        struct Span {
            ClipId id;
            double start = 0.0;
            double end = 0.0;
        };
        std::vector<Span> overlapping;
        for (const auto& clip : track->clips) {
            const double end = clip.startBeat + clip.lengthBeats;
            if (clip.startBeat < endBeat && end > startBeat)
                overlapping.push_back({clip.id, clip.startBeat, end});
        }

        for (const auto& span : overlapping) {
            if (span.start >= startBeat && span.end <= endBeat) {
                changed |= removeClip(span.id);
            } else if (span.start < startBeat && span.end > endBeat) {
                changed |= splitClipAtTempo(span.id, endBeat - span.start, secondsPerBeat).second.isValid();
                changed |= trimClipEnd(span.id, startBeat);
            } else if (span.start < startBeat) {
                changed |= trimClipEnd(span.id, startBeat);
            } else {
                changed |= trimClipStart(span.id, endBeat, secondsPerBeat);
            }
        }

        if (!closeGap)
            continue;
        const auto* after = findTrack(trackId);
        if (after == nullptr)
            continue;
        std::vector<std::pair<ClipId, double>> later;
        for (const auto& clip : after->clips)
            if (clip.startBeat >= endBeat)
                later.emplace_back(clip.id, clip.startBeat);
        const double gap = endBeat - startBeat;
        for (const auto& [id, start] : later)
            changed |= moveClip(id, std::max(0.0, start - gap));
    }
    return changed;
}

// The detached copy a range copy captures. Same cut rule as the doc edits above (so what is pasted
// is exactly what a delete of the same range would have removed): notes clipped and re-based to
// the fragment's own start, an audio fragment's sourceStartSeconds advanced by the part cut off its
// left, and the fade at any cut edge zeroed while a surviving original edge keeps its own. The id
// and every other field are the source clip's — the copy is not in any doc, so nothing resolves it.
std::optional<Clip> TimelineDoc::clipToRange(const Clip& clip, double startBeat, double endBeat,
                                             double secondsPerBeat) {
    if (!isValidBeatRange(startBeat, endBeat) || !isFinitePositive(secondsPerBeat))
        return std::nullopt;
    const double clipEnd = clip.startBeat + clip.lengthBeats;
    if (!(clip.startBeat < endBeat && clipEnd > startBeat))
        return std::nullopt;

    Clip fragment = clip;
    const double newStart = std::max(clip.startBeat, startBeat);
    const double newEnd = std::min(clipEnd, endBeat);

    if (newStart > clip.startBeat) {
        const double cut = newStart - clip.startBeat;
        std::vector<MidiNote> dropped;
        std::vector<MidiNote> kept;
        partitionNotesAt(fragment.notes, cut, keepOriginalId, dropped, kept);
        fragment.notes = std::move(kept);
        fragment.fadeInBeats = 0.0;
        if (fragment.assetRef.isNotEmpty())
            fragment.sourceStartSeconds += cut * secondsPerBeat;
    }
    if (newEnd < clipEnd) {
        std::vector<MidiNote> kept;
        std::vector<MidiNote> dropped;
        partitionNotesAt(fragment.notes, newEnd - newStart, keepOriginalId, kept, dropped);
        fragment.notes = std::move(kept);
        fragment.fadeOutBeats = 0.0;
    }
    fragment.startBeat = newStart;
    fragment.lengthBeats = newEnd - newStart;
    return fragment;
}

} // namespace synth
