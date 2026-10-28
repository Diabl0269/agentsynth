// The clip edge sizing rule. It is a free function rather than a TimelineDoc member because the clip lane
// paints a drag's preview from it while the document still holds the clip as it was; the commit then runs
// the same function inside TimelineDoc::resizeClipEdge, so the two can never disagree.
#include "ClipEdgeSizing.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>

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

    switch (mode) {
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

std::vector<ClipNoteStart> clipNoteStarts(const std::vector<MidiNote>& notes) {
    std::vector<ClipNoteStart> out;
    out.reserve(notes.size());
    for (const auto& note : notes)
        out.push_back({note.id, note.startBeat});
    return out;
}

ClipNoteGlide classifyClipNoteGlide(double oldLength, double newLength, const std::vector<ClipNoteStart>& before,
                                    const std::vector<ClipNoteStart>& after) {
    constexpr double kEps = 1e-6;
    if (std::abs(oldLength - newLength) < kEps)
        return ClipNoteGlide::Shift; // a move keeps every note's place inside the clip
    const double ratio = oldLength > 0.0 ? newLength / oldLength : 1.0;
    bool anyCommon = false;
    bool allSame = true;
    bool allScaled = true;
    bool anyScaleWitness = false;
    std::unordered_map<std::int64_t, double> wasStart;
    wasStart.reserve(before.size());
    for (const auto& b : before)
        wasStart[b.id.value] = b.startBeat;
    for (const auto& now : after) {
        const auto was = wasStart.find(now.id.value);
        if (was == wasStart.end())
            continue;
        anyCommon = true;
        if (std::abs(was->second - now.startBeat) > kEps)
            allSame = false;
        if (was->second > kEps) { // a note at the clip's start does not move under a scale, so it says nothing
            anyScaleWitness = true;
            if (std::abs(now.startBeat - was->second * ratio) > kEps * std::max(1.0, was->second))
                allScaled = false;
        }
    }
    if (!anyCommon)
        return ClipNoteGlide::Stay;
    if (allSame)
        return ClipNoteGlide::Shift;
    return allScaled && anyScaleWitness ? ClipNoteGlide::Scale : ClipNoteGlide::Stay;
}

double glidedNoteX(ClipNoteGlide glide, double restingX, double restLeft, double restWidth, double drawnLeft,
                   double drawnWidth) {
    switch (glide) {
    case ClipNoteGlide::Stay:
        return restingX;
    case ClipNoteGlide::Shift:
        return restingX + (drawnLeft - restLeft);
    case ClipNoteGlide::Scale:
        return restWidth > 0.0 ? drawnLeft + (restingX - restLeft) * (drawnWidth / restWidth) : restingX;
    }
    return restingX;
}

// The window is the audio the clip plays now: from its source offset for its length in seconds at today's tempo. A
// ratio above 1 is a shorter clip, as in soundbrowser::LoopTarget.
std::optional<AudioStretchWindow> audioStretchWindow(const Clip& clip, double newLengthBeats, double secondsPerBeat) {
    if (clip.assetRef.isEmpty() || !std::isfinite(newLengthBeats) || !(newLengthBeats > 0.0) ||
        !std::isfinite(secondsPerBeat) || !(secondsPerBeat > 0.0) || !(clip.lengthBeats > 0.0))
        return std::nullopt;
    return AudioStretchWindow{clip.sourceStartSeconds, clip.lengthBeats * secondsPerBeat,
                              clip.lengthBeats / newLengthBeats};
}

// Built from the public mutators rather than one new TimelineDoc member: the caller's recorded change makes them one
// undo step, as importAudioFileToClip does with addClip and setClipAsset. The fades scale with the audio they cover.
bool applyAudioClipStretch(TimelineDoc& doc, ClipId id, double newStartBeat, double newLengthBeats,
                           const juce::String& stretchedRef) {
    const auto* clip = doc.getClip(id);
    if (clip == nullptr || clip->assetRef.isEmpty() || stretchedRef.isEmpty() || !(clip->lengthBeats > 0.0))
        return false;
    const double factor = newLengthBeats / clip->lengthBeats;
    const double fadeIn = clip->fadeInBeats * factor;
    const double fadeOut = clip->fadeOutBeats * factor;
    bool ok = doc.setClipAsset(id, stretchedRef, 0.0);
    ok = ok && doc.moveClip(id, newStartBeat);
    ok = ok && doc.resizeClip(id, newLengthBeats);
    return ok && doc.setClipFades(id, fadeIn, fadeOut);
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
        return "notes and audio scale to the new length";
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
