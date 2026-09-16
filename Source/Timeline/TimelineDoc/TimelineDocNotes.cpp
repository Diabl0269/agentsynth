// Concern: notes/MIDI content.
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>

namespace synth {

using namespace detail;

// ---------------------------------------------------------------------- notes --

NoteId TimelineDoc::addNote(ClipId clipId, const MidiNote& note) {
    auto* clip = findClip(clipId);
    if (clip == nullptr || !isValidNote(note))
        return {};
    if (static_cast<int>(clip->notes.size()) >= kMaxNotesPerClip)
        return {};

    return applyMutation([&] {
        MidiNote toInsert = note;
        toInsert.id = NoteId{nextNoteId++}; // ids are doc-assigned, never taken from the caller
        const auto pos = std::lower_bound(clip->notes.begin(), clip->notes.end(), toInsert, noteLess);
        return clip->notes.insert(pos, toInsert)->id;
    });
}

bool TimelineDoc::clearNotes(ClipId clipId) {
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    if (clip->notes.empty())
        return true;

    return applyMutation([&] {
        clip->notes.clear();
        return true;
    });
}

bool TimelineDoc::removeNote(NoteId id) {
    Clip* owner = nullptr;
    auto* note = findNote(id, &owner);
    if (note == nullptr)
        return false;

    return applyMutation([&] {
        owner->notes.erase(owner->notes.begin() + (note - owner->notes.data()));
        return true;
    });
}

bool TimelineDoc::moveNote(NoteId id, double newStartBeat, int newPitch) {
    if (!isFiniteAtOrAfterZero(newStartBeat) || newPitch < 0 || newPitch > 127)
        return false;
    Clip* owner = nullptr;
    auto* note = findNote(id, &owner);
    if (note == nullptr)
        return false;
    if (note->startBeat == newStartBeat && note->pitch == newPitch)
        return true;

    return applyMutation([&] {
        // Lift the note out and re-insert it at its new sorted position, rather than re-sorting
        // the whole clip.
        const auto index = note - owner->notes.data();
        MidiNote moved = std::move(*note);
        moved.startBeat = newStartBeat;
        moved.pitch = newPitch;
        owner->notes.erase(owner->notes.begin() + index);
        const auto pos = std::lower_bound(owner->notes.begin(), owner->notes.end(), moved, noteLess);
        owner->notes.insert(pos, std::move(moved));
        return true;
    });
}

bool TimelineDoc::resizeNote(NoteId id, double newLengthBeats) {
    if (!isFinitePositive(newLengthBeats))
        return false;
    auto* note = findNote(id);
    if (note == nullptr)
        return false;
    if (note->lengthBeats == newLengthBeats)
        return true;

    return applyMutation([&] {
        note->lengthBeats = newLengthBeats; // length doesn't participate in note ordering
        return true;
    });
}

bool TimelineDoc::setNoteVelocity(NoteId id, int velocity) {
    if (velocity < 1 || velocity > 127)
        return false;
    auto* note = findNote(id);
    if (note == nullptr)
        return false;
    if (note->velocity == velocity)
        return true;

    return applyMutation([&] {
        note->velocity = velocity;
        return true;
    });
}

bool TimelineDoc::setNoteMuted(NoteId id, bool muted) {
    auto* note = findNote(id);
    if (note == nullptr)
        return false;
    if (note->muted == muted)
        return true;

    return applyMutation([&] {
        note->muted = muted; // mute doesn't participate in note ordering
        return true;
    });
}

bool TimelineDoc::quantiseNotes(ClipId clipId, double gridBeats, double strength) {
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    if (!std::isfinite(gridBeats) || gridBeats <= 0.0)
        return false;
    if (!std::isfinite(strength))
        return false;

    const double clampedStrength = juce::jlimit(0.0, 1.0, strength);

    std::vector<double> newStarts(clip->notes.size());
    bool anyMoved = false;
    for (size_t i = 0; i < clip->notes.size(); ++i) {
        const double start = clip->notes[i].startBeat;
        const double nearestGrid = std::round(start / gridBeats) * gridBeats;
        double newStart = start + clampedStrength * (nearestGrid - start);
        newStart = std::max(0.0, newStart);
        newStarts[i] = newStart;
        if (newStart != start)
            anyMoved = true;
    }
    if (!anyMoved)
        return true; // nothing actually moves: no-op, no revision bump

    return applyMutation([&] {
        for (size_t i = 0; i < clip->notes.size(); ++i)
            clip->notes[i].startBeat = newStarts[i];
        // Lengths are untouched and every note moved independently, so the list needs a single
        // re-sort at the end rather than a re-position per note.
        std::stable_sort(clip->notes.begin(), clip->notes.end(), noteLess);
        return true;
    });
}

bool TimelineDoc::quantiseNoteLengths(ClipId clipId, double gridBeats) {
    auto* clip = findClip(clipId);
    if (clip == nullptr)
        return false;
    if (!std::isfinite(gridBeats) || gridBeats <= 0.0)
        return false;

    std::vector<double> newLengths(clip->notes.size());
    bool anyChanged = false;
    for (size_t i = 0; i < clip->notes.size(); ++i) {
        const double length = clip->notes[i].lengthBeats;
        double nearestGrid = std::round(length / gridBeats) * gridBeats;
        if (nearestGrid < gridBeats)
            nearestGrid = gridBeats; // floored at one grid unit -- a note can never become zero-length
        newLengths[i] = nearestGrid;
        if (nearestGrid != length)
            anyChanged = true;
    }
    if (!anyChanged)
        return true; // nothing actually changes: no-op, no revision bump

    return applyMutation([&] {
        for (size_t i = 0; i < clip->notes.size(); ++i)
            clip->notes[i].lengthBeats = newLengths[i];
        // Lengths don't participate in note ordering (see resizeNote), so no re-sort is needed.
        return true;
    });
}

} // namespace synth
