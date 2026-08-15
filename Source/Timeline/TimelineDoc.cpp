#include "TimelineDoc.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <limits>
#include <set>

namespace synth {

namespace {

// -- ordering invariants ------------------------------------------------------
// The two comparators the whole file sorts by. Kept here, in one place, because a mutation
// path and the loader that repairs a hand-edited file must agree on the order exactly.

bool clipLess(const Clip& a, const Clip& b) {
    if (a.startBeat != b.startBeat)
        return a.startBeat < b.startBeat;
    return a.id.value < b.id.value;
}

bool noteLess(const MidiNote& a, const MidiNote& b) {
    if (a.startBeat != b.startBeat)
        return a.startBeat < b.startBeat;
    if (a.pitch != b.pitch)
        return a.pitch < b.pitch;
    return a.id.value < b.id.value;
}

// -- validation ---------------------------------------------------------------
// Non-finite values are rejected everywhere, not just because they're meaningless musically:
// a NaN beat makes every comparator above non-transitive, which is undefined behaviour for
// std::sort and std::lower_bound.

bool isFiniteAtOrAfterZero(double v) noexcept { return std::isfinite(v) && v >= 0.0; }
bool isFinitePositive(double v) noexcept { return std::isfinite(v) && v > 0.0; }

bool isValidNote(const MidiNote& note) noexcept {
    return isFiniteAtOrAfterZero(note.startBeat) && isFinitePositive(note.lengthBeats) && note.pitch >= 0 &&
           note.pitch <= 127 && note.velocity >= 1 && note.velocity <= 127 && note.channel >= 1 && note.channel <= 16;
}

bool isValidRange(const AutomationLane::RangeSnapshot& range) noexcept {
    return std::isfinite(range.minValue) && std::isfinite(range.maxValue) && std::isfinite(range.defaultValue) &&
           range.minValue <= range.maxValue;
}

bool isValidCurve(int curve) noexcept {
    return curve >= static_cast<int>(BreakpointCurve::Hold) && curve <= static_cast<int>(BreakpointCurve::Bezier);
}

bool isValidRecordMode(int mode) noexcept {
    return mode >= static_cast<int>(LaneRecordMode::Off) && mode <= static_cast<int>(LaneRecordMode::Write);
}

AutomationLane::Breakpoint makeBreakpoint(const AutomationLane::RangeSnapshot& range, double beat, double value,
                                          float tension, int curve) {
    AutomationLane::Breakpoint point;
    point.beat = beat;
    // Values are stored denormalised, so the lane's captured range is the only thing that can
    // bound them.
    point.value = juce::jlimit(static_cast<double>(range.minValue), static_cast<double>(range.maxValue), value);
    point.tension = juce::jlimit(-1.0f, 1.0f, tension);
    point.curve = curve;
    return point;
}

// First point at or after `beat`. The list is sorted, so every beat lookup is a binary search.
std::vector<AutomationLane::Breakpoint>::iterator lowerBoundByBeat(std::vector<AutomationLane::Breakpoint>& points,
                                                                   double beat) {
    return std::lower_bound(points.begin(), points.end(), beat,
                            [](const AutomationLane::Breakpoint& p, double b) { return p.beat < b; });
}

// Inserts (or replaces, on an exact beat match) into an already-sorted point list.
void insertBreakpoint(std::vector<AutomationLane::Breakpoint>& points, const AutomationLane::Breakpoint& point) {
    const auto pos = lowerBoundByBeat(points, point.beat);
    if (pos != points.end() && pos->beat == point.beat)
        *pos = point;
    else
        points.insert(pos, point);
}

// -- juce::var readers ---------------------------------------------------------
// Loader rule: an ABSENT property takes the field's default; a PRESENT property must be
// well-typed and in range or the whole load fails. That keeps hand-authored files ergonomic
// without ever letting malformed data through.

bool readInt(const juce::var& v, int& out) {
    if (v.isInt()) {
        out = static_cast<int>(v);
        return true;
    }
    if (v.isInt64()) {
        const auto wide = static_cast<std::int64_t>(v);
        if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
            return false;
        out = static_cast<int>(wide);
        return true;
    }
    return false;
}

bool readInt64(const juce::var& v, std::int64_t& out) {
    if (v.isInt() || v.isInt64()) {
        out = static_cast<std::int64_t>(v);
        return true;
    }
    return false;
}

// Accepts ints too: a JSON writer is free to emit 4 rather than 4.0 for a whole-numbered beat.
bool readDouble(const juce::var& v, double& out) {
    if (v.isDouble() || v.isInt() || v.isInt64()) {
        out = static_cast<double>(v);
        return true;
    }
    return false;
}

bool readBool(const juce::var& v, bool& out) {
    if (!v.isBool())
        return false;
    out = static_cast<bool>(v);
    return true;
}

bool readString(const juce::var& v, juce::String& out) {
    if (!v.isString())
        return false;
    out = v.toString();
    return true;
}

bool readOptionalInt(const juce::var& v, int& out) { return v.isVoid() || readInt(v, out); }
bool readOptionalInt64(const juce::var& v, std::int64_t& out) { return v.isVoid() || readInt64(v, out); }
bool readOptionalDouble(const juce::var& v, double& out) { return v.isVoid() || readDouble(v, out); }
bool readOptionalBool(const juce::var& v, bool& out) { return v.isVoid() || readBool(v, out); }
bool readOptionalString(const juce::var& v, juce::String& out) { return v.isVoid() || readString(v, out); }

bool readOptionalFloat(const juce::var& v, float& out) {
    if (v.isVoid())
        return true;
    double asDouble = 0.0;
    if (!readDouble(v, asDouble) || !std::isfinite(asDouble))
        return false;
    out = static_cast<float>(asDouble);
    return true;
}

// A required, strictly positive id.
bool readId(const juce::var& v, std::int64_t& out) { return readInt64(v, out) && out > 0; }

// An optional next-id counter: absent leaves the caller's default, present must be >= 1.
bool readOptionalCounter(const juce::var& v, std::int64_t& out) {
    if (v.isVoid())
        return true;
    return readInt64(v, out) && out >= 1;
}

// Absent -> empty list; present must be an array.
bool readOptionalArray(const juce::var& v, const juce::Array<juce::var>*& out) {
    if (v.isVoid())
        return true;
    if (!v.isArray())
        return false;
    out = v.getArray();
    return out != nullptr;
}

} // namespace

// ------------------------------------------------------------------ listeners --

void TimelineDoc::addListener(Listener* listener) {
    if (listener != nullptr)
        listeners.add(listener);
}

void TimelineDoc::removeListener(Listener* listener) {
    if (listener != nullptr)
        listeners.remove(listener);
}

void TimelineDoc::finishMutation() {
    ++revision;
    listeners.call([this](Listener& l) { l.timelineChanged(*this); });
}

// -------------------------------------------------------------------- lookups --

Track* TimelineDoc::findTrack(TrackId id) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks)
        if (track.id == id)
            return &track;
    return nullptr;
}

const Track* TimelineDoc::findTrack(TrackId id) const { return const_cast<TimelineDoc*>(this)->findTrack(id); }

Clip* TimelineDoc::findClip(ClipId id, Track** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            if (clip.id == id) {
                if (ownerOut != nullptr)
                    *ownerOut = &track;
                return &clip;
            }
        }
    }
    return nullptr;
}

MidiNote* TimelineDoc::findNote(NoteId id, Clip** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& clip : track.clips) {
            for (auto& note : clip.notes) {
                if (note.id == id) {
                    if (ownerOut != nullptr)
                        *ownerOut = &clip;
                    return &note;
                }
            }
        }
    }
    return nullptr;
}

AutomationLane* TimelineDoc::findLane(LaneId id, Track** ownerOut) {
    if (!id.isValid())
        return nullptr;
    for (auto& track : tracks) {
        for (auto& lane : track.lanes) {
            if (lane.id == id) {
                if (ownerOut != nullptr)
                    *ownerOut = &track;
                return &lane;
            }
        }
    }
    return nullptr;
}

AutomationLane* TimelineDoc::findLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) {
    for (auto& track : tracks)
        for (auto& lane : track.lanes)
            if (lane.nodeUuid == nodeUuid && lane.paramId == paramId)
                return &lane;
    return nullptr;
}

const Track* TimelineDoc::getTrack(TrackId id) const { return findTrack(id); }

const Clip* TimelineDoc::getClip(ClipId id) const { return const_cast<TimelineDoc*>(this)->findClip(id); }

const Track* TimelineDoc::getTrackForClip(ClipId id) const {
    Track* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findClip(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const MidiNote* TimelineDoc::getNote(NoteId id) const { return const_cast<TimelineDoc*>(this)->findNote(id); }

const Clip* TimelineDoc::getClipForNote(NoteId id) const {
    Clip* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findNote(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const AutomationLane* TimelineDoc::getLane(LaneId id) const { return const_cast<TimelineDoc*>(this)->findLane(id); }

const Track* TimelineDoc::getTrackForLane(LaneId id) const {
    Track* owner = nullptr;
    if (const_cast<TimelineDoc*>(this)->findLane(id, &owner) == nullptr)
        return nullptr;
    return owner;
}

const AutomationLane* TimelineDoc::getLaneForParam(const juce::String& nodeUuid, const juce::String& paramId) const {
    return const_cast<TimelineDoc*>(this)->findLaneForParam(nodeUuid, paramId);
}

// --------------------------------------------------------------------- tracks --

TrackId TimelineDoc::addTrack(TrackKind kind, const juce::String& name) {
    if (static_cast<int>(tracks.size()) >= kMaxTracks)
        return {};
    switch (kind) {
    case TrackKind::Midi:
    case TrackKind::Audio:
    case TrackKind::Automation:
        break;
    default:
        return {};
    }

    return applyMutation([&] {
        Track track;
        track.id = TrackId{nextTrackId++};
        track.kind = kind;
        track.name = name;
        tracks.push_back(std::move(track));
        return tracks.back().id;
    });
}

bool TimelineDoc::removeTrack(TrackId id) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;

    return applyMutation([&] {
        tracks.erase(tracks.begin() + (track - tracks.data()));
        return true;
    });
}

bool TimelineDoc::setTrackName(TrackId id, const juce::String& name) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->name == name)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        track->name = name;
        return true;
    });
}

bool TimelineDoc::setTrackColour(TrackId id, juce::uint32 colourArgb) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->colourArgb == colourArgb)
        return true;
    return applyMutation([&] {
        track->colourArgb = colourArgb;
        return true;
    });
}

bool TimelineDoc::setTrackMuted(TrackId id, bool muted) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->muted == muted)
        return true;
    return applyMutation([&] {
        track->muted = muted;
        return true;
    });
}

bool TimelineDoc::setTrackSoloed(TrackId id, bool soloed) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->soloed == soloed)
        return true;
    return applyMutation([&] {
        track->soloed = soloed;
        return true;
    });
}

bool TimelineDoc::setTrackArmed(TrackId id, bool armed) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->armed == armed)
        return true;
    return applyMutation([&] {
        track->armed = armed;
        return true;
    });
}

bool TimelineDoc::setTrackBinding(TrackId id, const juce::String& nodeUuid) {
    auto* track = findTrack(id);
    if (track == nullptr)
        return false;
    if (track->bindingUuid == nodeUuid)
        return true;
    return applyMutation([&] {
        track->bindingUuid = nodeUuid;
        // Optimistic: whatever the track's prior orphan state was, a changed binding hasn't been
        // checked against the graph yet. reconcileBindings re-derives the truth on the next pass;
        // in the meantime an empty nodeUuid is "unbound" (never orphaned) and a non-empty one is
        // presumed live rather than left flagged against a target it no longer even names.
        track->orphaned = false;
        return true;
    });
}

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

std::pair<ClipId, ClipId> TimelineDoc::splitClip(ClipId id, double atBeat) {
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

        // Partition the existing (sorted) notes into the two halves. A note straddling the
        // boundary is split in two: the left half keeps the original note's id, the right half
        // gets a fresh one. Re-based/truncated notes stay individually sorted by the transform
        // (a uniform shift or a length change never reorders a run), but the two halves have to
        // be re-sorted against EACH OTHER once combined: a straddling note's right half lands at
        // beat 0 alongside any entirely-right note that also started exactly at the boundary,
        // and pitch/id must still break the tie correctly.
        std::vector<MidiNote> leftNotes;
        std::vector<MidiNote> rightNotes;
        leftNotes.reserve(clip->notes.size());
        rightNotes.reserve(clip->notes.size());

        for (const auto& note : clip->notes) {
            const double noteEnd = note.startBeat + note.lengthBeats;
            if (noteEnd <= atBeat) {
                leftNotes.push_back(note);
            } else if (note.startBeat >= atBeat) {
                MidiNote moved = note;
                moved.startBeat -= atBeat;
                rightNotes.push_back(moved);
            } else {
                MidiNote left = note;
                left.lengthBeats = atBeat - note.startBeat;
                leftNotes.push_back(left);

                MidiNote right = note;
                right.id = NoteId{nextNoteId++};
                right.startBeat = 0.0;
                right.lengthBeats = noteEnd - atBeat;
                rightNotes.push_back(right);
            }
        }
        std::stable_sort(leftNotes.begin(), leftNotes.end(), noteLess);
        std::stable_sort(rightNotes.begin(), rightNotes.end(), noteLess);

        clip->notes = std::move(leftNotes);
        clip->lengthBeats = atBeat;

        Clip right;
        right.id = ClipId{nextClipId++};
        right.name = rightName;
        right.startBeat = rightStart;
        right.lengthBeats = rightLength;
        right.notes = std::move(rightNotes);

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
        clipA->lengthBeats = newEnd - clipA->startBeat;

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

// ---------------------------------------------------------------------- lanes --

LaneId TimelineDoc::addLane(TrackId trackId, const juce::String& nodeUuid, const juce::String& paramId,
                            const AutomationLane::RangeSnapshot& range) {
    // Identity check first, and doc-wide: one lane per bound parameter, whichever track it
    // happens to sit on. Returning the existing id is a lookup, not a mutation.
    if (auto* existing = findLaneForParam(nodeUuid, paramId))
        return existing->id;

    auto* track = findTrack(trackId);
    if (track == nullptr)
        return {};
    if (nodeUuid.isEmpty() || paramId.isEmpty() || !isValidRange(range))
        return {};
    if (static_cast<int>(track->lanes.size()) >= kMaxLanesPerTrack)
        return {};

    return applyMutation([&] {
        AutomationLane lane;
        lane.id = LaneId{nextLaneId++};
        lane.nodeUuid = nodeUuid;
        lane.paramId = paramId;
        lane.range = range;
        track->lanes.push_back(std::move(lane));
        return track->lanes.back().id;
    });
}

bool TimelineDoc::removeLane(LaneId id) {
    Track* owner = nullptr;
    auto* lane = findLane(id, &owner);
    if (lane == nullptr)
        return false;

    return applyMutation([&] {
        owner->lanes.erase(owner->lanes.begin() + (lane - owner->lanes.data()));
        return true;
    });
}

bool TimelineDoc::addBreakpoint(LaneId laneId, double beat, double value, float tension, int curve) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    if (!isFiniteAtOrAfterZero(beat) || !std::isfinite(value) || !std::isfinite(tension) || !isValidCurve(curve))
        return false;

    const auto point = makeBreakpoint(lane->range, beat, value, tension, curve);
    // Replacing an existing point doesn't grow the lane, so it stays legal at the cap.
    const auto existing = lowerBoundByBeat(lane->points, beat);
    const bool replacesExisting = existing != lane->points.end() && existing->beat == beat;
    if (!replacesExisting && static_cast<int>(lane->points.size()) >= kMaxBreakpointsPerLane)
        return false;

    return applyMutation([&] {
        insertBreakpoint(lane->points, point);
        return true;
    });
}

bool TimelineDoc::removeBreakpoint(LaneId laneId, double beat) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    const auto pos = lowerBoundByBeat(lane->points, beat);
    if (pos == lane->points.end() || pos->beat != beat)
        return false;

    return applyMutation([&] {
        lane->points.erase(pos);
        return true;
    });
}

bool TimelineDoc::editBreakpoints(LaneId laneId, const std::vector<double>& removeBeats,
                                  const std::vector<AutomationLane::Breakpoint>& addPoints) {
    auto* lane = findLane(laneId);
    if (lane == nullptr)
        return false;
    if (removeBeats.empty() && addPoints.empty())
        return true; // nothing to do: no-op, no revision bump

    for (const auto& p : addPoints)
        if (!isFiniteAtOrAfterZero(p.beat) || !std::isfinite(p.value) || !std::isfinite(p.tension) ||
            !isValidCurve(p.curve))
            return false;

    // Plan against a COPY first — same "simulate, then commit" shape as splitClip/reconcileBindings
    // — so a cap violation is rejected before the live lane is ever touched.
    auto simulated = lane->points;
    for (double beat : removeBeats) {
        const auto pos = lowerBoundByBeat(simulated, beat);
        if (pos != simulated.end() && pos->beat == beat)
            simulated.erase(pos);
    }
    for (const auto& p : addPoints)
        insertBreakpoint(simulated, makeBreakpoint(lane->range, p.beat, p.value, p.tension, p.curve));

    if (static_cast<int>(simulated.size()) > kMaxBreakpointsPerLane)
        return false;

    return applyMutation([&] {
        lane->points = std::move(simulated);
        return true;
    });
}

bool TimelineDoc::setLaneRecordMode(LaneId id, int mode) {
    auto* lane = findLane(id);
    if (lane == nullptr || !isValidRecordMode(mode))
        return false;
    if (lane->recordMode == mode)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        lane->recordMode = mode;
        return true;
    });
}

// -------------------------------------------------------- bindings / TL2-6 --

bool TimelineDoc::reconcileBindings(const std::function<bool(const juce::String& uuid)>& uuidResolves) {
    // Plan first, exactly like splitClip/applySnapshotPreservingNodes: compute what every flag
    // SHOULD be without touching anything, so a reconcile that changes nothing never enters
    // applyMutation (no bump, no notification) and uuidResolves is called exactly once per
    // binding rather than once per binding per pass.
    struct TrackPlan {
        bool orphaned = false;
        std::vector<bool> laneOrphaned;
    };

    std::vector<TrackPlan> plans;
    plans.reserve(tracks.size());
    bool anyChanged = false;

    for (auto& track : tracks) {
        TrackPlan plan;
        plan.orphaned = track.bindingUuid.isNotEmpty() && !uuidResolves(track.bindingUuid);
        if (plan.orphaned != track.orphaned)
            anyChanged = true;

        plan.laneOrphaned.reserve(track.lanes.size());
        for (auto& lane : track.lanes) {
            const bool laneOrphaned = lane.nodeUuid.isNotEmpty() && !uuidResolves(lane.nodeUuid);
            plan.laneOrphaned.push_back(laneOrphaned);
            if (laneOrphaned != lane.orphaned)
                anyChanged = true;
        }
        plans.push_back(std::move(plan));
    }

    if (!anyChanged)
        return false; // every flag already matches: no bump, no notification

    return applyMutation([&] {
        for (size_t i = 0; i < tracks.size(); ++i) {
            tracks[i].orphaned = plans[i].orphaned;
            for (size_t j = 0; j < tracks[i].lanes.size(); ++j)
                tracks[i].lanes[j].orphaned = plans[i].laneOrphaned[j];
        }
        return true;
    });
}

bool TimelineDoc::rebindLane(LaneId id, const juce::String& newNodeUuid) {
    Track* owner = nullptr;
    auto* lane = findLane(id, &owner);
    if (lane == nullptr || newNodeUuid.isEmpty())
        return false;

    // Doc-wide one-lane-per-parameter invariant: reject if some OTHER lane already owns
    // (newNodeUuid, this lane's paramId). The lane being rebound is allowed to "collide" with
    // itself (rebinding to the uuid it already has is a legal no-op path below).
    if (auto* existing = findLaneForParam(newNodeUuid, lane->paramId))
        if (existing->id != id)
            return false;

    if (lane->nodeUuid == newNodeUuid && !lane->orphaned)
        return true; // already bound here and already resolved: no-op, no bump

    return applyMutation([&] {
        lane->nodeUuid = newNodeUuid;
        // Optimistic, same reasoning as setTrackBinding: the next reconcileBindings re-derives
        // whether this uuid actually resolves.
        lane->orphaned = false;
        return true;
    });
}

// ----------------------------------------------------------------- doc-level --

void TimelineDoc::clear() {
    if (tracks.empty())
        return;
    applyMutation([&] { tracks.clear(); });
}

// -------------------------------------------------------------- serialisation --

juce::var TimelineDoc::toVar() const {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kFormatVersion);
    root->setProperty("nextTrackId", nextTrackId);
    root->setProperty("nextClipId", nextClipId);
    root->setProperty("nextLaneId", nextLaneId);
    root->setProperty("nextNoteId", nextNoteId);

    juce::Array<juce::var> trackVars;
    for (const auto& track : tracks) {
        juce::DynamicObject::Ptr t = new juce::DynamicObject();
        t->setProperty("id", track.id.value);
        t->setProperty("kind", static_cast<int>(track.kind));
        t->setProperty("name", track.name);
        t->setProperty("colourArgb", static_cast<std::int64_t>(track.colourArgb));
        t->setProperty("muted", track.muted);
        t->setProperty("soloed", track.soloed);
        t->setProperty("armed", track.armed);
        t->setProperty("bindingUuid", track.bindingUuid);

        juce::Array<juce::var> clipVars;
        for (const auto& clip : track.clips) {
            juce::DynamicObject::Ptr c = new juce::DynamicObject();
            c->setProperty("id", clip.id.value);
            c->setProperty("name", clip.name);
            c->setProperty("startBeat", clip.startBeat);
            c->setProperty("lengthBeats", clip.lengthBeats);

            juce::Array<juce::var> noteVars;
            for (const auto& note : clip.notes) {
                juce::DynamicObject::Ptr n = new juce::DynamicObject();
                n->setProperty("id", note.id.value);
                n->setProperty("startBeat", note.startBeat);
                n->setProperty("lengthBeats", note.lengthBeats);
                n->setProperty("pitch", note.pitch);
                n->setProperty("velocity", note.velocity);
                n->setProperty("channel", note.channel);
                noteVars.add(juce::var(n.get()));
            }
            c->setProperty("notes", noteVars);
            clipVars.add(juce::var(c.get()));
        }
        t->setProperty("clips", clipVars);

        juce::Array<juce::var> laneVars;
        for (const auto& lane : track.lanes) {
            juce::DynamicObject::Ptr l = new juce::DynamicObject();
            l->setProperty("id", lane.id.value);
            l->setProperty("nodeUuid", lane.nodeUuid);
            l->setProperty("paramId", lane.paramId);
            l->setProperty("recordMode", lane.recordMode);

            juce::DynamicObject::Ptr r = new juce::DynamicObject();
            r->setProperty("minValue", static_cast<double>(lane.range.minValue));
            r->setProperty("maxValue", static_cast<double>(lane.range.maxValue));
            r->setProperty("defaultValue", static_cast<double>(lane.range.defaultValue));
            l->setProperty("range", juce::var(r.get()));

            juce::Array<juce::var> pointVars;
            for (const auto& point : lane.points) {
                juce::DynamicObject::Ptr p = new juce::DynamicObject();
                p->setProperty("beat", point.beat);
                p->setProperty("value", point.value);
                p->setProperty("tension", static_cast<double>(point.tension));
                p->setProperty("curve", point.curve);
                pointVars.add(juce::var(p.get()));
            }
            l->setProperty("points", pointVars);
            laneVars.add(juce::var(l.get()));
        }
        t->setProperty("lanes", laneVars);

        trackVars.add(juce::var(t.get()));
    }
    root->setProperty("tracks", trackVars);

    return juce::var(root.get());
}

bool TimelineDoc::fromVar(const juce::var& state) {
    auto* rootObj = state.getDynamicObject();
    if (rootObj == nullptr)
        return false;

    int version = 0;
    if (!readInt(rootObj->getProperty("version"), version) || version != kFormatVersion)
        return false;

    std::int64_t parsedNextTrackId = 1;
    std::int64_t parsedNextClipId = 1;
    std::int64_t parsedNextLaneId = 1;
    std::int64_t parsedNextNoteId = 1;
    if (!readOptionalCounter(rootObj->getProperty("nextTrackId"), parsedNextTrackId) ||
        !readOptionalCounter(rootObj->getProperty("nextClipId"), parsedNextClipId) ||
        !readOptionalCounter(rootObj->getProperty("nextLaneId"), parsedNextLaneId) ||
        !readOptionalCounter(rootObj->getProperty("nextNoteId"), parsedNextNoteId))
        return false;

    const juce::Array<juce::var>* trackList = nullptr;
    if (!readOptionalArray(rootObj->getProperty("tracks"), trackList))
        return false;

    // Everything below builds into `parsed`; the live doc isn't touched until the very last
    // step, which is what makes a malformed field a clean no-op rather than a half-load.
    std::vector<Track> parsed;
    std::set<std::int64_t> seenTrackIds;
    std::set<std::int64_t> seenClipIds;
    std::set<std::int64_t> seenLaneIds;
    std::set<std::int64_t> seenNoteIds;
    std::set<std::pair<juce::String, juce::String>> seenLaneParams;

    if (trackList != nullptr) {
        if (trackList->size() > kMaxTracks)
            return false;
        parsed.reserve(static_cast<size_t>(trackList->size()));

        for (const auto& trackVar : *trackList) {
            auto* tObj = trackVar.getDynamicObject();
            if (tObj == nullptr)
                return false;

            Track track;
            std::int64_t trackIdValue = 0;
            if (!readId(tObj->getProperty("id"), trackIdValue) || !seenTrackIds.insert(trackIdValue).second)
                return false;
            track.id = TrackId{trackIdValue};

            int kindValue = static_cast<int>(TrackKind::Midi);
            if (!readOptionalInt(tObj->getProperty("kind"), kindValue))
                return false;
            // Kinds 3..15 are reserved: refuse a file this build can't represent rather than
            // coercing it into a kind we do understand.
            if (kindValue < static_cast<int>(TrackKind::Midi) || kindValue > static_cast<int>(TrackKind::Automation))
                return false;
            track.kind = static_cast<TrackKind>(kindValue);

            std::int64_t colourValue = static_cast<std::int64_t>(track.colourArgb);
            if (!readOptionalString(tObj->getProperty("name"), track.name) ||
                !readOptionalInt64(tObj->getProperty("colourArgb"), colourValue) ||
                !readOptionalBool(tObj->getProperty("muted"), track.muted) ||
                !readOptionalBool(tObj->getProperty("soloed"), track.soloed) ||
                !readOptionalBool(tObj->getProperty("armed"), track.armed) ||
                !readOptionalString(tObj->getProperty("bindingUuid"), track.bindingUuid))
                return false;
            if (colourValue < 0 || colourValue > 0xffffffffLL)
                return false;
            track.colourArgb = static_cast<juce::uint32>(colourValue);

            const juce::Array<juce::var>* clipList = nullptr;
            if (!readOptionalArray(tObj->getProperty("clips"), clipList))
                return false;
            if (clipList != nullptr) {
                if (clipList->size() > kMaxClipsPerTrack)
                    return false;
                track.clips.reserve(static_cast<size_t>(clipList->size()));

                for (const auto& clipVar : *clipList) {
                    auto* cObj = clipVar.getDynamicObject();
                    if (cObj == nullptr)
                        return false;

                    Clip clip;
                    std::int64_t clipIdValue = 0;
                    if (!readId(cObj->getProperty("id"), clipIdValue) || !seenClipIds.insert(clipIdValue).second)
                        return false;
                    clip.id = ClipId{clipIdValue};

                    if (!readOptionalString(cObj->getProperty("name"), clip.name) ||
                        !readOptionalDouble(cObj->getProperty("startBeat"), clip.startBeat) ||
                        !readOptionalDouble(cObj->getProperty("lengthBeats"), clip.lengthBeats))
                        return false;
                    if (!isFiniteAtOrAfterZero(clip.startBeat) || !isFinitePositive(clip.lengthBeats))
                        return false;

                    const juce::Array<juce::var>* noteList = nullptr;
                    if (!readOptionalArray(cObj->getProperty("notes"), noteList))
                        return false;
                    if (noteList != nullptr) {
                        if (noteList->size() > kMaxNotesPerClip)
                            return false;
                        clip.notes.reserve(static_cast<size_t>(noteList->size()));

                        for (const auto& noteVar : *noteList) {
                            auto* nObj = noteVar.getDynamicObject();
                            if (nObj == nullptr)
                                return false;
                            MidiNote note;
                            std::int64_t noteIdValue = 0;
                            // Required, not optional: the dialect never shipped without note ids,
                            // so a file missing one is malformed, not old-format.
                            if (!readId(nObj->getProperty("id"), noteIdValue) ||
                                !seenNoteIds.insert(noteIdValue).second)
                                return false;
                            note.id = NoteId{noteIdValue};
                            if (!readOptionalDouble(nObj->getProperty("startBeat"), note.startBeat) ||
                                !readOptionalDouble(nObj->getProperty("lengthBeats"), note.lengthBeats) ||
                                !readOptionalInt(nObj->getProperty("pitch"), note.pitch) ||
                                !readOptionalInt(nObj->getProperty("velocity"), note.velocity) ||
                                !readOptionalInt(nObj->getProperty("channel"), note.channel))
                                return false;
                            if (!isValidNote(note))
                                return false;
                            clip.notes.push_back(note);
                        }
                        // Repaired, not trusted: a hand-edited file must not be able to hand a
                        // reader an unsorted note list.
                        std::stable_sort(clip.notes.begin(), clip.notes.end(), noteLess);
                    }
                    track.clips.push_back(std::move(clip));
                }
                std::stable_sort(track.clips.begin(), track.clips.end(), clipLess);
            }

            const juce::Array<juce::var>* laneList = nullptr;
            if (!readOptionalArray(tObj->getProperty("lanes"), laneList))
                return false;
            if (laneList != nullptr) {
                if (laneList->size() > kMaxLanesPerTrack)
                    return false;
                track.lanes.reserve(static_cast<size_t>(laneList->size()));

                for (const auto& laneVar : *laneList) {
                    auto* lObj = laneVar.getDynamicObject();
                    if (lObj == nullptr)
                        return false;

                    AutomationLane lane;
                    std::int64_t laneIdValue = 0;
                    if (!readId(lObj->getProperty("id"), laneIdValue) || !seenLaneIds.insert(laneIdValue).second)
                        return false;
                    lane.id = LaneId{laneIdValue};

                    if (!readString(lObj->getProperty("nodeUuid"), lane.nodeUuid) ||
                        !readString(lObj->getProperty("paramId"), lane.paramId))
                        return false;
                    if (lane.nodeUuid.isEmpty() || lane.paramId.isEmpty())
                        return false;
                    // The doc-wide one-lane-per-parameter rule is an invariant of the model, so
                    // a file that breaks it is malformed, not something to silently merge.
                    if (!seenLaneParams.insert({lane.nodeUuid, lane.paramId}).second)
                        return false;

                    const juce::var rangeVar = lObj->getProperty("range");
                    if (auto* rObj = rangeVar.getDynamicObject()) {
                        if (!readOptionalFloat(rObj->getProperty("minValue"), lane.range.minValue) ||
                            !readOptionalFloat(rObj->getProperty("maxValue"), lane.range.maxValue) ||
                            !readOptionalFloat(rObj->getProperty("defaultValue"), lane.range.defaultValue))
                            return false;
                    } else if (!rangeVar.isVoid()) {
                        return false;
                    }
                    if (!isValidRange(lane.range))
                        return false;

                    // TL4-4. Absent (a file written before record modes existed) => the default
                    // already on `lane`, which is Read. Present but out of range is malformed, not
                    // something to clamp: the value ends up in the snapshot the applier switches on.
                    if (!readOptionalInt(lObj->getProperty("recordMode"), lane.recordMode))
                        return false;
                    if (!isValidRecordMode(lane.recordMode))
                        return false;

                    const juce::Array<juce::var>* pointList = nullptr;
                    if (!readOptionalArray(lObj->getProperty("points"), pointList))
                        return false;
                    if (pointList != nullptr) {
                        if (pointList->size() > kMaxBreakpointsPerLane)
                            return false;
                        lane.points.reserve(static_cast<size_t>(pointList->size()));

                        for (const auto& pointVar : *pointList) {
                            auto* pObj = pointVar.getDynamicObject();
                            if (pObj == nullptr)
                                return false;
                            double beat = 0.0;
                            double value = 0.0;
                            float tension = 0.0f;
                            int curve = static_cast<int>(BreakpointCurve::Linear);
                            if (!readOptionalDouble(pObj->getProperty("beat"), beat) ||
                                !readOptionalDouble(pObj->getProperty("value"), value) ||
                                !readOptionalFloat(pObj->getProperty("tension"), tension) ||
                                !readOptionalInt(pObj->getProperty("curve"), curve))
                                return false;
                            if (!isFiniteAtOrAfterZero(beat) || !std::isfinite(value) || !isValidCurve(curve))
                                return false;
                            lane.points.push_back(makeBreakpoint(lane.range, beat, value, tension, curve));
                        }
                        std::stable_sort(lane.points.begin(), lane.points.end(),
                                         [](const AutomationLane::Breakpoint& a, const AutomationLane::Breakpoint& b) {
                                             return a.beat < b.beat;
                                         });
                        // Same-beat duplicates collapse the way a second addBreakpoint would:
                        // the last one in the file wins.
                        std::vector<AutomationLane::Breakpoint> deduped;
                        deduped.reserve(lane.points.size());
                        for (const auto& point : lane.points) {
                            if (!deduped.empty() && deduped.back().beat == point.beat)
                                deduped.back() = point;
                            else
                                deduped.push_back(point);
                        }
                        lane.points = std::move(deduped);
                    }
                    track.lanes.push_back(std::move(lane));
                }
            }
            parsed.push_back(std::move(track));
        }
    }

    // Counters are floored at one past the highest id actually present: a hand-edited file that
    // lowers a counter must not be able to make the doc hand out an id it's already using.
    for (const auto& track : parsed) {
        parsedNextTrackId = std::max(parsedNextTrackId, track.id.value + 1);
        for (const auto& clip : track.clips) {
            parsedNextClipId = std::max(parsedNextClipId, clip.id.value + 1);
            for (const auto& note : clip.notes)
                parsedNextNoteId = std::max(parsedNextNoteId, note.id.value + 1);
        }
        for (const auto& lane : track.lanes)
            parsedNextLaneId = std::max(parsedNextLaneId, lane.id.value + 1);
    }

    return applyMutation([&] {
        tracks = std::move(parsed);
        nextTrackId = parsedNextTrackId;
        nextClipId = parsedNextClipId;
        nextLaneId = parsedNextLaneId;
        nextNoteId = parsedNextNoteId;
        return true;
    });
}

} // namespace synth
