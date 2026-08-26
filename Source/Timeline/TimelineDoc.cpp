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

bool markerLess(const Marker& a, const Marker& b) {
    if (a.beat != b.beat)
        return a.beat < b.beat;
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

// The asset-reference rule, in one place because setClipAsset and fromVar must agree
// EXACTLY — a path the mutation API refuses must not be loadable from a file, or a hand-edited
// bundle becomes the way around the check. See Clip::assetRef for the threat.
//
// Rejected: a leading '/' or '\' (absolute POSIX / UNC), a Windows drive letter ("C:..."), any
// segment that is exactly ".." (escapes the bundle root), and any embedded NUL. Everything else —
// including a plain file name with no directory — is accepted, because a bundle-relative path is
// resolved against the bundle root and nothing else.
bool isValidAssetRefString(const juce::String& ref) noexcept {
    if (ref.isEmpty())
        return true; // "no asset": what every MIDI clip carries

    if (ref.containsChar('\0'))
        return false;
    const juce::juce_wchar first = ref[0];
    if (first == '/' || first == '\\')
        return false;
    // "C:", "C:/", "C:\..." — a drive-relative path is absolute enough to escape the bundle.
    if (ref.length() >= 2 && ref[1] == ':')
        return false;

    // Both separators are checked: a bundle written on Windows and opened on macOS must be
    // rejected by the same rule, not merely mis-resolved.
    juce::StringArray segments;
    segments.addTokens(ref.replaceCharacter('\\', '/'), "/", {});
    for (const auto& segment : segments)
        if (segment == "..")
            return false;

    return true;
}

// A marker label is capped but never trimmed or rewritten — see TimelineDoc::addMarker.
bool isValidMarkerText(const juce::String& text) noexcept { return text.length() <= TimelineDoc::kMaxMarkerTextLength; }

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
        // Route through juce::int64: on LP64 Linux std::int64_t is `long`, which juce::var can
        // convert to via BOTH its int and int64 operators — a direct cast is ambiguous there.
        const auto wide = static_cast<std::int64_t>(static_cast<juce::int64>(v));
        if (wide < std::numeric_limits<int>::min() || wide > std::numeric_limits<int>::max())
            return false;
        out = static_cast<int>(wide);
        return true;
    }
    return false;
}

bool readInt64(const juce::var& v, std::int64_t& out) {
    if (v.isInt() || v.isInt64()) {
        out = static_cast<std::int64_t>(static_cast<juce::int64>(v));
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

// A required, strictly positive id, bounded above so the next `id + 1` allocation can never
// signed-overflow.
bool readId(const juce::var& v, std::int64_t& out) {
    return readInt64(v, out) && out > 0 && out <= TimelineDoc::kMaxIdValue;
}

// An optional next-id counter: absent leaves the caller's default, present must be in
// [1, kMaxIdValue] for the same overflow reason as readId.
bool readOptionalCounter(const juce::var& v, std::int64_t& out) {
    if (v.isVoid())
        return true;
    return readInt64(v, out) && out >= 1 && out <= TimelineDoc::kMaxIdValue;
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

Marker* TimelineDoc::findMarker(MarkerId id) {
    for (auto& marker : markers)
        if (marker.id == id)
            return &marker;
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

const Marker* TimelineDoc::getMarker(MarkerId id) const { return const_cast<TimelineDoc*>(this)->findMarker(id); }

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
        // The halves keep pointing at the same asset with the same gain, and
        // each keeps the fade at the edge it still owns (the left half's fade-out and the right
        // half's fade-in are at the cut, where there is nothing to fade). `sourceStartSeconds` is
        // deliberately COPIED UNCHANGED rather than advanced by the split offset: converting
        // `atBeat` to seconds needs a tempo map, and this document has none by design (see the
        // class comment). Re-seating the right half's source offset needs that tempo map, so it
        // is left for later work.
        right.assetRef = clip->assetRef;
        right.gainDb = clip->gainDb;
        right.fadeOutBeats = clip->fadeOutBeats;
        right.sourceStartSeconds = clip->sourceStartSeconds;
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

// ---------------------------------------------------------------------- lanes --

LaneId TimelineDoc::addLane(TrackId trackId, const juce::String& nodeUuid, const juce::String& paramId,
                            const AutomationLane::RangeSnapshot& range, int paramIndexHint) {
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
        lane.paramIndexHint = paramIndexHint;
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

// -------------------------------------------------------- bindings --

bool TimelineDoc::reconcileBindings(const std::function<bool(const juce::String& uuid)>& uuidResolves,
                                    const std::function<bool(const juce::String& uuid, const juce::String& paramId,
                                                             int paramIndexHint)>& laneResolves) {
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
        // A track binds to a node, never a parameter — uuidResolves alone is always the whole story.
        plan.orphaned = track.bindingUuid.isNotEmpty() && !uuidResolves(track.bindingUuid);
        if (plan.orphaned != track.orphaned)
            anyChanged = true;

        plan.laneOrphaned.reserve(track.lanes.size());
        for (auto& lane : track.lanes) {
            // laneResolves (when the caller supplied one) is the richer predicate that also
            // accounts for a HostedPluginModule's parameter set having changed shape; unset, this is
            // just the uuid-only check.
            const bool resolved = laneResolves ? laneResolves(lane.nodeUuid, lane.paramId, lane.paramIndexHint)
                                               : uuidResolves(lane.nodeUuid);
            const bool laneOrphaned = lane.nodeUuid.isNotEmpty() && !resolved;
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

// -------------------------------------------------------------------- markers --

MarkerId TimelineDoc::addMarker(double beat, const juce::String& text, juce::uint32 colourArgb) {
    if (!isFiniteAtOrAfterZero(beat) || !isValidMarkerText(text))
        return {};
    if (static_cast<int>(markers.size()) >= kMaxMarkers)
        return {};

    return applyMutation([&] {
        Marker marker;
        marker.id = MarkerId{nextMarkerId++};
        marker.beat = beat;
        marker.text = text;
        marker.colourArgb = colourArgb;
        const auto pos = std::lower_bound(markers.begin(), markers.end(), marker, markerLess);
        return markers.insert(pos, std::move(marker))->id;
    });
}

bool TimelineDoc::removeMarker(MarkerId id) {
    auto* marker = findMarker(id);
    if (marker == nullptr)
        return false;

    return applyMutation([&] {
        markers.erase(markers.begin() + (marker - markers.data()));
        return true;
    });
}

bool TimelineDoc::setMarkerText(MarkerId id, const juce::String& text) {
    auto* marker = findMarker(id);
    if (marker == nullptr || !isValidMarkerText(text))
        return false;
    if (marker->text == text)
        return true; // already there: no revision bump, no notification
    return applyMutation([&] {
        marker->text = text;
        return true;
    });
}

bool TimelineDoc::setMarkerColour(MarkerId id, juce::uint32 colourArgb) {
    auto* marker = findMarker(id);
    if (marker == nullptr)
        return false;
    if (marker->colourArgb == colourArgb)
        return true;
    return applyMutation([&] {
        marker->colourArgb = colourArgb;
        return true;
    });
}

bool TimelineDoc::moveMarker(MarkerId id, double newBeat) {
    auto* marker = findMarker(id);
    if (marker == nullptr || !isFiniteAtOrAfterZero(newBeat))
        return false;
    if (marker->beat == newBeat)
        return true;

    return applyMutation([&] {
        // Copy out, erase, re-insert at the sorted position — the same re-seat moveClip does, so
        // the (beat, id) order holds without re-sorting the whole vector.
        Marker moved = *marker;
        moved.beat = newBeat;
        markers.erase(markers.begin() + (marker - markers.data()));
        const auto pos = std::lower_bound(markers.begin(), markers.end(), moved, markerLess);
        markers.insert(pos, std::move(moved));
        return true;
    });
}

// ----------------------------------------------------------------- doc-level --

void TimelineDoc::clear() {
    if (tracks.empty() && markers.empty())
        return;
    applyMutation([&] {
        tracks.clear();
        markers.clear();
    });
}

// -------------------------------------------------------------- serialisation --

juce::var TimelineDoc::toVar() const {
    juce::DynamicObject::Ptr root = new juce::DynamicObject();
    root->setProperty("version", kFormatVersion);
    // 64-bit values are written as juce::int64: juce::var has no `long` overload, so an LP64
    // std::int64_t (Linux) is ambiguous between the int and int64 constructors.
    root->setProperty("nextTrackId", static_cast<juce::int64>(nextTrackId));
    root->setProperty("nextClipId", static_cast<juce::int64>(nextClipId));
    root->setProperty("nextLaneId", static_cast<juce::int64>(nextLaneId));
    root->setProperty("nextNoteId", static_cast<juce::int64>(nextNoteId));
    root->setProperty("nextMarkerId", static_cast<juce::int64>(nextMarkerId));

    juce::Array<juce::var> trackVars;
    for (const auto& track : tracks) {
        juce::DynamicObject::Ptr t = new juce::DynamicObject();
        t->setProperty("id", static_cast<juce::int64>(track.id.value));
        t->setProperty("kind", static_cast<int>(track.kind));
        t->setProperty("name", track.name);
        t->setProperty("colourArgb", static_cast<juce::int64>(track.colourArgb));
        t->setProperty("muted", track.muted);
        t->setProperty("soloed", track.soloed);
        t->setProperty("armed", track.armed);
        t->setProperty("bindingUuid", track.bindingUuid);

        juce::Array<juce::var> clipVars;
        for (const auto& clip : track.clips) {
            juce::DynamicObject::Ptr c = new juce::DynamicObject();
            c->setProperty("id", static_cast<juce::int64>(clip.id.value));
            c->setProperty("name", clip.name);
            c->setProperty("startBeat", clip.startBeat);
            c->setProperty("lengthBeats", clip.lengthBeats);
            // Additive, same "written ALWAYS" rule as the audio fields below: a reader that
            // predates it ignores the key, and one that has it gets a single shape to parse.
            c->setProperty("muted", clip.muted);
            // Audio fields, written ALWAYS (not only when non-default): a reader that
            // predates them ignores unknown keys, and a reader that has them gets one shape to
            // parse rather than two. Additive — kFormatVersion stays 1.
            c->setProperty("assetRef", clip.assetRef);
            c->setProperty("gainDb", clip.gainDb);
            c->setProperty("fadeInBeats", clip.fadeInBeats);
            c->setProperty("fadeOutBeats", clip.fadeOutBeats);
            c->setProperty("sourceStartSeconds", clip.sourceStartSeconds);

            juce::Array<juce::var> noteVars;
            for (const auto& note : clip.notes) {
                juce::DynamicObject::Ptr n = new juce::DynamicObject();
                n->setProperty("id", static_cast<juce::int64>(note.id.value));
                n->setProperty("startBeat", note.startBeat);
                n->setProperty("lengthBeats", note.lengthBeats);
                n->setProperty("pitch", note.pitch);
                n->setProperty("velocity", note.velocity);
                n->setProperty("channel", note.channel);
                n->setProperty("muted", note.muted); // additive; absent loads as false
                noteVars.add(juce::var(n.get()));
            }
            c->setProperty("notes", noteVars);
            clipVars.add(juce::var(c.get()));
        }
        t->setProperty("clips", clipVars);

        juce::Array<juce::var> laneVars;
        for (const auto& lane : track.lanes) {
            juce::DynamicObject::Ptr l = new juce::DynamicObject();
            l->setProperty("id", static_cast<juce::int64>(lane.id.value));
            l->setProperty("nodeUuid", lane.nodeUuid);
            l->setProperty("paramId", lane.paramId);
            l->setProperty("recordMode", lane.recordMode);
            l->setProperty("paramIndexHint", lane.paramIndexHint); // additive

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

    // Written ALWAYS (an empty array when there are none), the same one-shape-to-parse rule the
    // clips' audio fields follow. Additive — kFormatVersion stays 1.
    juce::Array<juce::var> markerVars;
    for (const auto& marker : markers) {
        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        m->setProperty("id", static_cast<juce::int64>(marker.id.value));
        m->setProperty("beat", marker.beat);
        m->setProperty("text", marker.text);
        m->setProperty("colourArgb", static_cast<juce::int64>(marker.colourArgb));
        markerVars.add(juce::var(m.get()));
    }
    root->setProperty("markers", markerVars);

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
    std::int64_t parsedNextMarkerId = 1;
    if (!readOptionalCounter(rootObj->getProperty("nextTrackId"), parsedNextTrackId) ||
        !readOptionalCounter(rootObj->getProperty("nextClipId"), parsedNextClipId) ||
        !readOptionalCounter(rootObj->getProperty("nextLaneId"), parsedNextLaneId) ||
        !readOptionalCounter(rootObj->getProperty("nextNoteId"), parsedNextNoteId) ||
        !readOptionalCounter(rootObj->getProperty("nextMarkerId"), parsedNextMarkerId))
        return false;

    const juce::Array<juce::var>* trackList = nullptr;
    if (!readOptionalArray(rootObj->getProperty("tracks"), trackList))
        return false;

    // Everything below builds into `parsed`; the live doc isn't touched until the very last
    // step, which is what makes a malformed field a clean no-op rather than a half-load.
    std::vector<Track> parsed;
    std::vector<Marker> parsedMarkers;
    std::set<std::int64_t> seenTrackIds;
    std::set<std::int64_t> seenClipIds;
    std::set<std::int64_t> seenLaneIds;
    std::set<std::int64_t> seenNoteIds;
    std::set<std::int64_t> seenMarkerIds;
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

                    // `muted` is optional like the rest: a file written before per-clip mute
                    // existed simply has no key, and the struct default (false — audible) is what
                    // that file always meant.
                    if (!readOptionalString(cObj->getProperty("name"), clip.name) ||
                        !readOptionalDouble(cObj->getProperty("startBeat"), clip.startBeat) ||
                        !readOptionalDouble(cObj->getProperty("lengthBeats"), clip.lengthBeats) ||
                        !readOptionalBool(cObj->getProperty("muted"), clip.muted))
                        return false;
                    if (!isFiniteAtOrAfterZero(clip.startBeat) || !isFinitePositive(clip.lengthBeats))
                        return false;

                    // Audio fields. All optional — absent means the struct default, which is
                    // what an older clip with no audio fields loads as. A PRESENT but illegal
                    // value is malformed, not something to clamp: `assetRef` is the security rule
                    // (see isValidAssetRefString — a hand-edited bundle must not be the way around
                    // setClipAsset's check), and a NaN fade would poison the renderer downstream.
                    if (!readOptionalString(cObj->getProperty("assetRef"), clip.assetRef) ||
                        !readOptionalDouble(cObj->getProperty("gainDb"), clip.gainDb) ||
                        !readOptionalDouble(cObj->getProperty("fadeInBeats"), clip.fadeInBeats) ||
                        !readOptionalDouble(cObj->getProperty("fadeOutBeats"), clip.fadeOutBeats) ||
                        !readOptionalDouble(cObj->getProperty("sourceStartSeconds"), clip.sourceStartSeconds))
                        return false;
                    if (!isValidAssetRefString(clip.assetRef) || !std::isfinite(clip.gainDb) ||
                        !isFiniteAtOrAfterZero(clip.fadeInBeats) || !isFiniteAtOrAfterZero(clip.fadeOutBeats) ||
                        !isFiniteAtOrAfterZero(clip.sourceStartSeconds))
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
                                !readOptionalInt(nObj->getProperty("channel"), note.channel) ||
                                !readOptionalBool(nObj->getProperty("muted"), note.muted))
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

                    // Absent (a file written before record modes existed) => the default
                    // already on `lane`, which is Read. Present but out of range is malformed, not
                    // something to clamp: the value ends up in the snapshot the applier switches on.
                    if (!readOptionalInt(lObj->getProperty("recordMode"), lane.recordMode))
                        return false;
                    if (!isValidRecordMode(lane.recordMode))
                        return false;

                    // Additive: absent (every file written before this field existed) keeps
                    // the -1 default already on `lane`. No range check beyond "must be an integer if
                    // present" — resolveLaneParameter treats anything < 0 as "no hint" and bounds-
                    // checks a non-negative one defensively, so there is nothing here that can turn a
                    // malformed value into an out-of-bounds read downstream.
                    if (!readOptionalInt(lObj->getProperty("paramIndexHint"), lane.paramIndexHint))
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

    // Markers. Absent (every file written before they existed) means none — additive, so no
    // version bump. Same loader rule as everything above: an absent field takes its default, a
    // present one must be well-typed AND in range, and the sort order is repaired rather than
    // trusted.
    const juce::Array<juce::var>* markerList = nullptr;
    if (!readOptionalArray(rootObj->getProperty("markers"), markerList))
        return false;
    if (markerList != nullptr) {
        if (markerList->size() > kMaxMarkers)
            return false;
        parsedMarkers.reserve(static_cast<size_t>(markerList->size()));

        for (const auto& markerVar : *markerList) {
            auto* mObj = markerVar.getDynamicObject();
            if (mObj == nullptr)
                return false;

            Marker marker;
            std::int64_t markerIdValue = 0;
            if (!readId(mObj->getProperty("id"), markerIdValue) || !seenMarkerIds.insert(markerIdValue).second)
                return false;
            marker.id = MarkerId{markerIdValue};

            std::int64_t colourValue = static_cast<std::int64_t>(marker.colourArgb);
            if (!readOptionalDouble(mObj->getProperty("beat"), marker.beat) ||
                !readOptionalString(mObj->getProperty("text"), marker.text) ||
                !readOptionalInt64(mObj->getProperty("colourArgb"), colourValue))
                return false;
            if (!isFiniteAtOrAfterZero(marker.beat) || !isValidMarkerText(marker.text))
                return false;
            if (colourValue < 0 || colourValue > 0xffffffffLL)
                return false;
            marker.colourArgb = static_cast<juce::uint32>(colourValue);

            parsedMarkers.push_back(std::move(marker));
        }
        std::stable_sort(parsedMarkers.begin(), parsedMarkers.end(), markerLess);
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
    for (const auto& marker : parsedMarkers)
        parsedNextMarkerId = std::max(parsedNextMarkerId, marker.id.value + 1);

    return applyMutation([&] {
        tracks = std::move(parsed);
        markers = std::move(parsedMarkers);
        nextTrackId = parsedNextTrackId;
        nextClipId = parsedNextClipId;
        nextLaneId = parsedNextLaneId;
        nextNoteId = parsedNextNoteId;
        nextMarkerId = parsedNextMarkerId;
        return true;
    });
}

} // namespace synth
