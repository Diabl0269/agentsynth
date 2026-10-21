// Concern: linked clips — MIDI clips that share one musical content.
//
// What a link shares follows the "linked / shared clip" model of the common DAWs: the CONTENT is
// shared (the notes, the clip's length and the drum machine's steps per bar and per-drum mute/solo), while everything
// about where and how a copy plays is its own (name, position, track, mute, gain, fades). Editing the notes of any
// member — through any note mutator, from the piano roll, quantise, the velocity lane or an import — rewrites every
// other member inside the SAME applyMutation, so the edit is one revision, one notification and therefore one undo step
// (AppUndoManager diffs the snapshots around the mutation).
//
// Note ids stay unique doc-wide: the other members get fresh copies of the edited clip's notes,
// issued in its (already sorted) order, so each member's list is sorted by (startBeat, pitch, id)
// with no re-sort. A member's note ids therefore change whenever another member is edited.
//
// Structure edits that cut a clip (split, glue, left-edge resize, range trims) produce content the
// group does not share, so the clip leaves its group; a group left with one clip is dissolved by
// dissolveLoneLinkGroups, which finishMutation runs after every mutation (a load included). Group
// ids are drawn from the clip-id counter, which is persisted and never reissues a value, so a new
// group can never collide with a live one and no extra counter needs a place in the file format.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <map>

namespace synth {

using namespace detail;

ClipId TimelineDoc::duplicateClipLinked(ClipId id) { return duplicateClipImpl(id, true); }

bool TimelineDoc::makeClipUnique(ClipId id) {
    auto* clip = findClip(id);
    if (clip == nullptr || clip->linkGroup == 0)
        return false;

    return applyMutation([&] {
        clip->linkGroup = 0; // keeps its notes; a lone survivor is unlinked by finishMutation
        return true;
    });
}

int TimelineDoc::countLinkedClips(ClipId id) const {
    const auto* clip = getClip(id);
    if (clip == nullptr || clip->linkGroup == 0)
        return 0;
    int others = 0;
    for (const auto& track : tracks)
        for (const auto& other : track.clips)
            if (other.linkGroup == clip->linkGroup && other.id != clip->id)
                ++others;
    return others;
}

// Called at the end of a mutation's lambda, after `source` is edited. Only the other members'
// notes, length and drum-machine settings are reassigned — no clip is inserted or erased — so every Clip/Track pointer
// the calling mutator still holds stays valid.
void TimelineDoc::syncLinkedClips(const Clip& source) {
    if (source.linkGroup == 0)
        return;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            if (clip.linkGroup != source.linkGroup || clip.id == source.id)
                continue;
            clip.lengthBeats = source.lengthBeats;
            clip.stepsPerBar = source.stepsPerBar;
            clip.mutedPitches = source.mutedPitches;
            clip.soloPitches = source.soloPitches;
            clip.notes.clear();
            clip.notes.reserve(source.notes.size());
            for (const auto& note : source.notes) {
                MidiNote copy = note;
                copy.id = NoteId{nextNoteId++};
                clip.notes.push_back(copy);
            }
        }
    }
}

// Keeps the invariant "a non-zero link group has at least two clips, all of them MIDI": an audio
// clip (an asset set on a linked clip, a hand-edited file) is unlinked, and so is the last member
// of a group whose other clips were deleted, cut or made unique.
void TimelineDoc::dissolveLoneLinkGroups() {
    std::map<std::int64_t, int> sizes;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            if (clip.linkGroup == 0)
                continue;
            if (clip.assetRef.isNotEmpty())
                clip.linkGroup = 0;
            else
                ++sizes[clip.linkGroup];
        }
    }
    if (sizes.empty())
        return;
    for (auto& track : tracks)
        for (auto& clip : track.clips)
            if (clip.linkGroup != 0 && sizes[clip.linkGroup] < 2)
                clip.linkGroup = 0;
}

} // namespace synth
