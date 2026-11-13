#include "Timeline/FitClipsToKey.h"
#include "Timeline/ClipKey.h"
#include "Timeline/FitToKey.h"
#include <algorithm>
#include <map>
#include <set>

namespace synth {

namespace {

std::vector<int> sortedPitches(const Clip& clip) {
    std::vector<int> pitches;
    pitches.reserve(clip.notes.size());
    for (const auto& note : clip.notes)
        pitches.push_back(note.pitch);
    std::sort(pitches.begin(), pitches.end());
    return pitches;
}

// Looked up by id at the moment of the fit: fitting one linked clip rewrites its siblings' notes with new ids, so ids
// taken earlier for a sibling would no longer exist.
bool fitOneClip(TimelineDoc& doc, ClipId id, const MusicalScale& scale) {
    const auto* clip = doc.getClip(id);
    if (clip == nullptr)
        return false;
    return fitNotesToKey(doc, fitToKeyTargets(*clip, {}), scale);
}

} // namespace

bool fitClipsToTheirKeys(TimelineDoc& doc, const std::vector<ClipId>& ids, const std::optional<MusicalScale>& songKey) {
    bool moved = false;
    for (const auto id : ids) {
        const auto* clip = doc.getClip(id);
        if (clip == nullptr || clip->assetRef.isNotEmpty())
            continue;
        const auto key = resolveClipKey(*clip, songKey).key;
        if (key.has_value())
            moved = fitOneClip(doc, id, *key) || moved;
    }
    return moved;
}

FitProjectResult fitProjectToSongKey(TimelineDoc& doc, const MusicalScale& songKey,
                                     const std::function<bool(const Track&)>& skipTrack) {
    FitProjectResult result;
    std::set<std::int64_t> blockedGroups; // link groups holding a clip that must not change
    std::vector<ClipId> candidates;       // MIDI clips with notes on tracks that may change
    for (const auto& track : doc.getTracks()) {
        const bool skipped = track.kind != TrackKind::Midi || (skipTrack && skipTrack(track));
        for (const auto& clip : track.clips) {
            if (clip.assetRef.isNotEmpty()) {
                ++result.audioClips;
                continue;
            }
            if (skipped) {
                if (clip.linkGroup != 0)
                    blockedGroups.insert(clip.linkGroup);
                continue;
            }
            if (clip.scale.has_value() && clip.linkGroup != 0)
                blockedGroups.insert(clip.linkGroup);
            if (!clip.notes.empty())
                candidates.push_back(clip.id);
        }
    }

    std::map<ClipId, std::vector<int>> before;
    std::vector<ClipId> eligible;
    for (const auto id : candidates) {
        const auto& clip = *doc.getClip(id);
        const bool ownKey = clip.scale.has_value() || (clip.linkGroup != 0 && blockedGroups.count(clip.linkGroup) > 0);
        if (ownKey)
            ++result.skippedOwnKey;
        else {
            eligible.push_back(id);
            before[id] = sortedPitches(clip);
        }
    }

    std::set<std::int64_t> doneGroups; // a linked group is fitted once: its siblings already share the result
    for (const auto id : eligible) {
        const auto group = doc.getClip(id)->linkGroup;
        if (group != 0 && !doneGroups.insert(group).second)
            continue;
        fitOneClip(doc, id, songKey);
    }

    result.clipsFollowing = (int)eligible.size();
    for (const auto id : eligible)
        if (const auto* clip = doc.getClip(id); clip != nullptr && sortedPitches(*clip) != before[id])
            ++result.clipsMoved;
    return result;
}

juce::String describeProjectFit(const FitProjectResult& result, const juce::String& keyName) {
    juce::String text;
    if (result.clipsMoved > 0)
        text = "Fitted " + juce::String(result.clipsMoved) + (result.clipsMoved == 1 ? " clip to " : " clips to ") +
               keyName + ".";
    else if (result.clipsFollowing > 0)
        text = "Nothing moved: every note in the clips that follow the song key is already in " + keyName + ".";
    else
        text = "Nothing moved: no MIDI clip follows the song key.";
    if (result.skippedOwnKey > 0)
        text += " Skipped " + juce::String(result.skippedOwnKey) + " with their own key.";
    if (result.audioClips > 0)
        text += " Audio clips were left alone.";
    return text;
}

} // namespace synth
