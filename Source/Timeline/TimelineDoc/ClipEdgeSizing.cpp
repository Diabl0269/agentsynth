// The clip edge sizing rule. It is a free function rather than a TimelineDoc member because the clip lane
// paints a drag's preview from it while the document still holds the clip as it was; the commit then runs
// the same function inside TimelineDoc::resizeClipEdge, so the two can never disagree.
#include "ClipEdgeSizing.h"
#include "TimelineDocInternal.h"

#include <algorithm>

namespace synth {

namespace {
// A stretched note never shrinks to nothing: the shortest one is a 1/1024 beat, far under any grid step.
constexpr double kMinStretchedNoteBeats = 1.0 / 1024.0;

// Notes keep their absolute beat. Moving the edge right cuts at the new edge by the shared partition rule
// (wholly-before notes go, a straddler keeps its right half and its id); moving it left only uncovers space.
std::vector<MidiNote> trimmedFromLeft(const std::vector<MidiNote>& notes, double delta) {
    std::vector<MidiNote> kept;
    if (delta > 0.0) {
        std::vector<MidiNote> dropped;
        detail::partitionNotesAt(notes, delta, [](const MidiNote& original) { return original.id; }, dropped, kept);
        return kept;
    }
    kept = notes;
    for (auto& note : kept)
        note.startBeat -= delta;
    return kept;
}

// Starts and lengths scale together, so the order (startBeat, pitch, id) is unchanged.
std::vector<MidiNote> stretched(const std::vector<MidiNote>& notes, double factor) {
    std::vector<MidiNote> out = notes;
    for (auto& note : out) {
        note.startBeat *= factor;
        note.lengthBeats = std::max(note.lengthBeats * factor, kMinStretchedNoteBeats);
    }
    return out;
}
} // namespace

// Right edge: the start stays, Trim and Carry contents both leave the notes alone (a note past the new end
// is kept, unheard, exactly as resizeClip always did). Left edge: Trim re-bases the notes so they stay on
// their beat, Carry contents leaves them clip-relative so they travel with the edge. Stretch scales either.
ClipEdgeResult clipEdgeResult(const Clip& clip, ClipEdge edge, ClipSizingMode mode, double newStartBeat,
                              double newLengthBeats) {
    ClipEdgeResult result;
    result.startBeat = edge == ClipEdge::Left ? newStartBeat : clip.startBeat;
    result.lengthBeats = newLengthBeats;
    if (!clip.assetRef.isEmpty())
        return result; // an audio clip has no notes to place

    switch (effectiveSizingMode(clip, mode)) {
    case ClipSizingMode::Trim:
        result.notes = edge == ClipEdge::Left ? trimmedFromLeft(clip.notes, newStartBeat - clip.startBeat) : clip.notes;
        break;
    case ClipSizingMode::CarryContents:
        result.notes = clip.notes;
        break;
    case ClipSizingMode::Stretch:
        result.notes = clip.lengthBeats > 0.0 ? stretched(clip.notes, newLengthBeats / clip.lengthBeats) : clip.notes;
        break;
    }
    return result;
}

ClipSizingMode effectiveSizingMode(const Clip& clip, ClipSizingMode mode) noexcept {
    if (mode == ClipSizingMode::Stretch && !clip.assetRef.isEmpty())
        return ClipSizingMode::Trim;
    return mode;
}

const char* clipSizingModeName(ClipSizingMode mode) noexcept {
    switch (mode) {
    case ClipSizingMode::Trim:
        return "Trim";
    case ClipSizingMode::CarryContents:
        return "Carry contents";
    case ClipSizingMode::Stretch:
        return "Stretch to fit";
    }
    return "Trim";
}

const char* clipSizingModeDescription(ClipSizingMode mode) noexcept {
    switch (mode) {
    case ClipSizingMode::Trim:
        return "notes stay put";
    case ClipSizingMode::CarryContents:
        return "notes move with the edge";
    case ClipSizingMode::Stretch:
        return "notes scale to the new length";
    }
    return "";
}

ClipSizingMode nextClipSizingMode(ClipSizingMode mode) noexcept {
    switch (mode) {
    case ClipSizingMode::Trim:
        return ClipSizingMode::CarryContents;
    case ClipSizingMode::CarryContents:
        return ClipSizingMode::Stretch;
    case ClipSizingMode::Stretch:
        return ClipSizingMode::Trim;
    }
    return ClipSizingMode::Trim;
}

const char* clipSizingModeKey(ClipSizingMode mode) noexcept {
    switch (mode) {
    case ClipSizingMode::Trim:
        return "trim";
    case ClipSizingMode::CarryContents:
        return "carry";
    case ClipSizingMode::Stretch:
        return "stretch";
    }
    return "trim";
}

std::optional<ClipSizingMode> clipSizingModeFromKey(const juce::String& key) {
    for (auto mode : {ClipSizingMode::Trim, ClipSizingMode::CarryContents, ClipSizingMode::Stretch})
        if (key == clipSizingModeKey(mode))
            return mode;
    return std::nullopt;
}

} // namespace synth
