// Concern: serialisation (toVar/fromVar JSON dialect).
#include "TimelineDoc.h"
#include "TimelineDocInternal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>

namespace synth {

using namespace detail;

namespace {

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
