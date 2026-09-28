// Concern: clips, including their audio (streamed-asset) fields.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <iterator>

namespace synth {

using namespace detail;

// ---------------------------------------------------------------------- clips --

ClipId TimelineDoc::addClip(TrackId trackId, double startBeat, double lengthBeats, const juce::String& name) {
    auto* track = findTrack(trackId);
    if (track == nullptr)
        return {};
    if (!isFiniteAtOrAfterZero(startBeat) || !isFinitePositive(lengthBeats))
        return {};
    if (static_cast<int>(track->clips.size()) >= kMaxClipsPerTrack)
        return {};

    return applyMutation([&] {
        Clip clip;
        clip.id = ClipId{nextClipId++};
        clip.name = name;
        clip.startBeat = startBeat;
        clip.lengthBeats = lengthBeats;
        const auto pos = std::lower_bound(track->clips.begin(), track->clips.end(), clip, clipLess);
        return track->clips.insert(pos, std::move(clip))->id;
    });
}

bool TimelineDoc::removeClip(ClipId id) {
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return false;

    return applyMutation([&] {
        owner->clips.erase(owner->clips.begin() + (clip - owner->clips.data()));
        return true;
    });
}

bool TimelineDoc::moveClip(ClipId id, double newStartBeat) {
    if (!isFiniteAtOrAfterZero(newStartBeat))
        return false;
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return false;
    if (clip->startBeat == newStartBeat)
        return true;

    return applyMutation([&] {
        // Lift the clip out and re-insert it at its new sorted position; notes ride along
        // untouched because they're stored clip-relative.
        const auto index = clip - owner->clips.data();
        Clip moved = std::move(*clip);
        moved.startBeat = newStartBeat;
        owner->clips.erase(owner->clips.begin() + index);
        const auto pos = std::lower_bound(owner->clips.begin(), owner->clips.end(), moved, clipLess);
        owner->clips.insert(pos, std::move(moved));
        return true;
    });
}

bool TimelineDoc::moveClipToTrack(ClipId id, TrackId destTrack, double newStartBeat) {
    if (!isFiniteAtOrAfterZero(newStartBeat))
        return false;
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return false;
    auto* dest = findTrack(destTrack);
    if (dest == nullptr)
        return false;

    // Dropping a clip back on its own track IS moveClip — including its no-op case, and including
    // the fact that it applies no kind check. An in-place drag must never fail on data the model
    // already tolerates (a note left on an audio track, an assetRef left on a MIDI clip).
    if (dest == owner)
        return moveClip(id, newStartBeat);

    // Cross-track only: the clip's payload has to match what the destination track plays. See
    // moveClipToTrack's declaration for why this one path is stricter than the rest of the model.
    const bool carriesAsset = clip->assetRef.isNotEmpty();
    if (carriesAsset ? dest->kind != TrackKind::Audio : dest->kind != TrackKind::Midi)
        return false;
    // The DESTINATION's cap, because that is the vector the clip ends up in — the same check
    // addClip makes before growing a track.
    if (static_cast<int>(dest->clips.size()) >= kMaxClipsPerTrack)
        return false;

    return applyMutation([&] {
        // Same lift-and-re-insert as moveClip, just landing in a different track's vector. `dest`
        // points into `tracks`, which erasing from owner->clips cannot invalidate, and notes ride
        // along untouched because they're stored clip-relative.
        const auto index = clip - owner->clips.data();
        Clip moved = std::move(*clip);
        moved.startBeat = newStartBeat;
        owner->clips.erase(owner->clips.begin() + index);
        const auto pos = std::lower_bound(dest->clips.begin(), dest->clips.end(), moved, clipLess);
        dest->clips.insert(pos, std::move(moved));
        return true;
    });
}

bool TimelineDoc::resizeClip(ClipId id, double newLengthBeats) {
    if (!isFinitePositive(newLengthBeats))
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->lengthBeats == newLengthBeats)
        return true;

    return applyMutation([&] {
        clip->lengthBeats = newLengthBeats; // length doesn't participate in the clip ordering
        return true;
    });
}

bool TimelineDoc::setClipName(ClipId id, const juce::String& name) {
    // Trimmed-then-rejected rather than trimmed-then-stored-blank: an empty title is
    // indistinguishable from a broken lane, and the inline rename editor's escape path is a cancel.
    // No length cap and nothing else sanitised — setTrackName applies neither, and the two must not
    // disagree about what a legal name is.
    const juce::String trimmed = name.trim();
    if (trimmed.isEmpty())
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->name == trimmed)
        return true; // already there: no revision bump, no notification

    return applyMutation([&] {
        clip->name = trimmed;
        return true;
    });
}

bool TimelineDoc::setClipMuted(ClipId id, bool muted) {
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->muted == muted)
        return true;

    return applyMutation([&] {
        clip->muted = muted;
        return true;
    });
}

std::pair<ClipId, ClipId> TimelineDoc::splitClip(ClipId id, double atBeat) { return splitClipImpl(id, atBeat, 0.0); }

// `secondsPerBeat` 0 is splitClip's documented behaviour: the right half's sourceStartSeconds is
// COPIED UNCHANGED, because converting `atBeat` to seconds needs a tempo map and this document has
// none by design (see the class comment). A positive value is the tempo-aware variant the range
// edits need (splitClipAtTempo): the caller supplies the tempo, and an audio right half then starts
// reading exactly where the left half stopped.
std::pair<ClipId, ClipId> TimelineDoc::splitClipImpl(ClipId id, double atBeat, double secondsPerBeat) {
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return {};
    if (!std::isfinite(atBeat) || atBeat <= 0.0 || atBeat >= clip->lengthBeats)
        return {};
    if (static_cast<int>(owner->clips.size()) >= kMaxClipsPerTrack)
        return {};

    return applyMutation([&]() -> std::pair<ClipId, ClipId> {
        const double rightStart = clip->startBeat + atBeat;
        const double rightLength = clip->lengthBeats - atBeat;
        const juce::String rightName = clip->name;

        // The shared cut rule (partitionNotesAt): a straddling note's left half keeps its id, the
        // right half gets a fresh one.
        std::vector<MidiNote> leftNotes;
        std::vector<MidiNote> rightNotes;
        partitionNotesAt(
            clip->notes, atBeat, [this](const MidiNote&) { return NoteId{nextNoteId++}; }, leftNotes, rightNotes);

        std::vector<ClipControllerLane> leftLanes;
        std::vector<ClipControllerLane> rightLanes;
        splitControllerLanes(clip->controllers, atBeat, leftLanes, rightLanes);
        clip->controllers = std::move(leftLanes);

        clip->notes = std::move(leftNotes);
        clip->lengthBeats = atBeat;

        Clip right;
        right.id = ClipId{nextClipId++};
        right.name = rightName;
        right.startBeat = rightStart;
        right.lengthBeats = rightLength;
        right.notes = std::move(rightNotes);
        right.controllers = std::move(rightLanes);
        // The halves keep pointing at the same asset with the same gain, and
        // each keeps the fade at the edge it still owns (the left half's fade-out and the right
        // half's fade-in are at the cut, where there is nothing to fade). `sourceStartSeconds` is
        // advanced only when the caller supplied a tempo (see above).
        right.assetRef = clip->assetRef;
        right.gainDb = clip->gainDb;
        right.fadeOutBeats = clip->fadeOutBeats;
        right.sourceStartSeconds = clip->sourceStartSeconds;
        if (secondsPerBeat > 0.0 && clip->assetRef.isNotEmpty())
            right.sourceStartSeconds += atBeat * secondsPerBeat;
        // Both halves inherit the mute: cutting a muted clip in two is a cut, not an un-mute of
        // half of it. (The left half keeps its own flag by simply not being rewritten.) Each note's
        // own muted flag rides along in the struct copies above, including the straddling note's
        // two halves.
        right.muted = clip->muted;
        clip->fadeOutBeats = 0.0;

        // `clip` (and therefore `id`, the original/left id) stays valid; only insert may
        // reallocate, and we don't dereference `clip` again after this point.
        const auto pos = std::lower_bound(owner->clips.begin(), owner->clips.end(), right, clipLess);
        const auto insertedIt = owner->clips.insert(pos, std::move(right));
        return std::make_pair(id, insertedIt->id);
    });
}

bool TimelineDoc::joinClips(ClipId a, ClipId b) {
    if (a == b)
        return false;
    Track* ownerA = nullptr;
    Track* ownerB = nullptr;
    auto* clipA = findClip(a, &ownerA);
    auto* clipB = findClip(b, &ownerB);
    if (clipA == nullptr || clipB == nullptr)
        return false;
    if (ownerA != ownerB)
        return false;
    if (!(clipA->startBeat < clipB->startBeat))
        return false;
    if (clipB->startBeat < clipA->startBeat + clipA->lengthBeats) // overlap
        return false;
    if (clipA->notes.size() + clipB->notes.size() > static_cast<size_t>(kMaxNotesPerClip))
        return false;
    std::vector<ClipControllerLane> mergedLanes;
    if (!mergeControllerLanes(clipA->controllers, clipB->controllers, clipB->startBeat - clipA->startBeat, mergedLanes))
        return false;

    return applyMutation([&] {
        const double rebase = clipB->startBeat - clipA->startBeat;
        const double newEnd = clipB->startBeat + clipB->lengthBeats;

        std::vector<MidiNote> rebasedB;
        rebasedB.reserve(clipB->notes.size());
        for (const auto& note : clipB->notes) {
            MidiNote moved = note;
            moved.startBeat += rebase;
            rebasedB.push_back(moved);
        }

        std::vector<MidiNote> merged;
        merged.reserve(clipA->notes.size() + rebasedB.size());
        std::merge(clipA->notes.begin(), clipA->notes.end(), rebasedB.begin(), rebasedB.end(),
                   std::back_inserter(merged), noteLess);
        clipA->notes = std::move(merged);
        clipA->controllers = std::move(mergedLanes);
        clipA->lengthBeats = newEnd - clipA->startBeat;
        // `a` keeps its OWN asset, gain, source offset and fade-in; `b`'s are dropped along
        // with `b`. Two audio clips naming different assets cannot become one clip naming both, so
        // "the survivor's asset wins" is the only answer that doesn't invent a crossfade. `a` does
        // inherit b's fade-OUT, because that edge is now a's. `a`'s `muted` is likewise left
        // untouched — the survivor's state is the one that survives — while each merged note keeps
        // its own muted flag through the struct copies above.
        clipA->fadeOutBeats = clipB->fadeOutBeats;

        // Erase b last: clipA and clipB alias the same vector, but nothing above dereferences
        // clipA or clipB again after this.
        ownerA->clips.erase(ownerA->clips.begin() + (clipB - ownerA->clips.data()));
        return true;
    });
}

ClipId TimelineDoc::duplicateClip(ClipId id) {
    Track* owner = nullptr;
    auto* clip = findClip(id, &owner);
    if (clip == nullptr)
        return {};
    if (static_cast<int>(owner->clips.size()) >= kMaxClipsPerTrack)
        return {};

    return applyMutation([&] {
        Clip dup;
        dup.id = ClipId{nextClipId++};
        dup.name = clip->name;
        dup.startBeat = clip->startBeat + clip->lengthBeats;
        dup.lengthBeats = clip->lengthBeats;
        // A duplicate plays the same asset, from the same offset, with the same gain and
        // fades. Nothing here needs a tempo map (unlike splitClip), so the copy is exact.
        dup.assetRef = clip->assetRef;
        dup.gainDb = clip->gainDb;
        dup.fadeInBeats = clip->fadeInBeats;
        dup.fadeOutBeats = clip->fadeOutBeats;
        dup.sourceStartSeconds = clip->sourceStartSeconds;
        // Copied field by field rather than by struct assignment (the ids must not be), so `muted`
        // has to be listed here explicitly — a duplicate of a muted clip is muted.
        dup.muted = clip->muted;
        dup.controllers = clip->controllers; // CC points are clip-relative, so they copy verbatim
        dup.notes.reserve(clip->notes.size());
        for (const auto& note : clip->notes) {
            MidiNote copy = note;
            copy.id = NoteId{nextNoteId++};
            dup.notes.push_back(copy);
        }
        // Ids are assigned in the same relative order as the source's already-sorted notes, so
        // the copy is sorted too — no re-sort needed.
        const auto pos = std::lower_bound(owner->clips.begin(), owner->clips.end(), dup, clipLess);
        return owner->clips.insert(pos, std::move(dup))->id;
    });
}

// ---------------------------------------------------------------- audio clips --

bool TimelineDoc::isValidAssetRef(const juce::String& ref) { return isValidAssetRefString(ref); }

bool TimelineDoc::setClipAsset(ClipId id, const juce::String& assetRef, double sourceStartSeconds) {
    if (!isValidAssetRefString(assetRef) || !isFiniteAtOrAfterZero(sourceStartSeconds))
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->assetRef == assetRef && clip->sourceStartSeconds == sourceStartSeconds)
        return true; // already there: no revision bump, no notification

    return applyMutation([&] {
        clip->assetRef = assetRef;
        clip->sourceStartSeconds = sourceStartSeconds;
        return true;
    });
}

bool TimelineDoc::setClipGainDb(ClipId id, double gainDb) {
    if (!std::isfinite(gainDb))
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->gainDb == gainDb)
        return true;

    return applyMutation([&] {
        clip->gainDb = gainDb;
        return true;
    });
}

bool TimelineDoc::setClipFades(ClipId id, double fadeInBeats, double fadeOutBeats) {
    if (!isFiniteAtOrAfterZero(fadeInBeats) || !isFiniteAtOrAfterZero(fadeOutBeats))
        return false;
    auto* clip = findClip(id);
    if (clip == nullptr)
        return false;
    if (clip->fadeInBeats == fadeInBeats && clip->fadeOutBeats == fadeOutBeats)
        return true;

    return applyMutation([&] {
        clip->fadeInBeats = fadeInBeats;
        clip->fadeOutBeats = fadeOutBeats;
        return true;
    });
}

} // namespace synth
