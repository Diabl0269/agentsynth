#include "TimelineValidator.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <utility>

namespace synth {

namespace {

using Error = TimelineValidationError;

TimelineValidationResult fail(Error error, const juce::String& message) { return {false, error, message}; }

// -- juce::var readers ---------------------------------------------------------------------
// Deliberately the same shape as TimelineDoc.cpp's readers — an ABSENT property takes the
// field's default, a PRESENT one must be well-typed — because a var this gate accepts has to be
// a var the loader then accepts. Where the two differ, they differ ON PURPOSE and the difference
// is commented at the check itself (untrusted-strict vs. the loader's clamp/repair).

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

bool readOptionalInt(const juce::var& v, int& out) { return v.isVoid() || readInt(v, out); }
bool readOptionalInt64(const juce::var& v, std::int64_t& out) { return v.isVoid() || readInt64(v, out); }
bool readOptionalDouble(const juce::var& v, double& out) { return v.isVoid() || readDouble(v, out); }
bool readOptionalBool(const juce::var& v, bool& out) {
    if (v.isVoid())
        return true;
    if (!v.isBool())
        return false;
    out = static_cast<bool>(v);
    return true;
}
bool readOptionalString(const juce::var& v, juce::String& out) {
    if (v.isVoid())
        return true;
    if (!v.isString())
        return false;
    out = v.toString();
    return true;
}
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

// Absent -> no list; present must be an array.
bool readOptionalArray(const juce::var& v, const juce::Array<juce::var>*& out) {
    if (v.isVoid())
        return true;
    if (!v.isArray())
        return false;
    out = v.getArray();
    return out != nullptr;
}

// -- shared predicates ----------------------------------------------------------------------

bool isBeatInBounds(double v) { return std::isfinite(v) && v >= 0.0 && v <= kMaxPpqUntrusted; }
bool isLengthInBounds(double v) { return std::isfinite(v) && v > 0.0 && v <= kMaxPpqUntrusted; }

// -- message helpers ------------------------------------------------------------------------
// Every rejection names the offending item the way validatePatch's do, so the message can be
// handed to a model (or shown to a user) as the correction to make, not just a complaint.

juce::String trackLabel(std::int64_t id, const juce::String& name) {
    return "Track " + juce::String(id) + (name.isNotEmpty() ? " (\"" + name + "\")" : juce::String());
}

juce::String clipLabel(std::int64_t id, const juce::String& name) {
    return "Clip " + juce::String(id) + (name.isNotEmpty() ? " (\"" + name + "\")" : juce::String());
}

juce::String beatRangeText() { return "0 and " + juce::String(kMaxPpqUntrusted); }

// The graph's uuid -> processor index. Built once per call: a lane resolution is a map lookup,
// not a walk of every node. A node with no "uuid" property resolves nothing, exactly as in
// TimelineReconciler — a binding is only ever created against a uuid a node already carries.
std::map<juce::String, juce::AudioProcessor*> indexGraphByUuid(const juce::AudioProcessorGraph& graph) {
    std::map<juce::String, juce::AudioProcessor*> byUuid;
    for (auto* node : graph.getNodes()) {
        const juce::String uuid = node->properties["uuid"].toString();
        if (uuid.isNotEmpty())
            byUuid.emplace(uuid, node->getProcessor());
    }
    return byUuid;
}

juce::RangedAudioParameter* findParameter(juce::AudioProcessor* processor, const juce::String& paramId) {
    if (processor == nullptr)
        return nullptr;
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramId)
                return ranged;
    return nullptr;
}

// -- per-container checks --------------------------------------------------------------------

TimelineValidationResult validateNote(const juce::var& noteVar, const juce::String& clipText,
                                      std::set<std::int64_t>& seenNoteIds) {
    auto* nObj = noteVar.getDynamicObject();
    if (nObj == nullptr)
        return fail(Error::MalformedRoot, "A note in " + clipText + " is not an object.");

    std::int64_t noteId = 0;
    if (!readId(nObj->getProperty("id"), noteId))
        return fail(Error::MalformedRoot, "A note in " + clipText + " is missing a positive integer \"id\".");
    if (!seenNoteIds.insert(noteId).second)
        return fail(Error::MalformedRoot, "Duplicate note id " + juce::String(noteId) + " (in " + clipText +
                                              "). Note ids must be unique across the whole timeline.");

    const juce::String noteText = "Note " + juce::String(noteId) + " in " + clipText;

    double startBeat = 0.0;
    double lengthBeats = 1.0;
    if (!readOptionalDouble(nObj->getProperty("startBeat"), startBeat) ||
        !readOptionalDouble(nObj->getProperty("lengthBeats"), lengthBeats))
        return fail(Error::MalformedRoot, noteText + " has a non-numeric \"startBeat\" or \"lengthBeats\".");
    if (!isBeatInBounds(startBeat))
        return fail(Error::BeatOutOfBounds, noteText + " starts at beat " + juce::String(startBeat) +
                                                ", which is not a finite beat between " + beatRangeText() +
                                                ". Note beats are relative to the clip's own start.");
    if (!isLengthInBounds(lengthBeats))
        return fail(Error::BeatOutOfBounds, noteText + " is " + juce::String(lengthBeats) +
                                                " beats long. A note length must be finite, greater than 0 and at "
                                                "most " +
                                                juce::String(kMaxPpqUntrusted) + ".");

    // Ranges are REJECTED, never clamped. The editing API clamps a user's drag because a hand on a
    // control means "as far as this goes"; a model naming pitch 128 means something this parameter
    // cannot express, and silently rewriting it to 127 ships a note nobody asked for.
    int pitch = 60;
    int velocity = 100;
    int channel = 1;
    if (!readOptionalInt(nObj->getProperty("pitch"), pitch) ||
        !readOptionalInt(nObj->getProperty("velocity"), velocity) ||
        !readOptionalInt(nObj->getProperty("channel"), channel))
        return fail(Error::MalformedRoot, noteText + " has a non-integer \"pitch\", \"velocity\" or \"channel\".");
    if (pitch < 0 || pitch > 127)
        return fail(Error::NoteOutOfRange,
                    noteText + " has pitch " + juce::String(pitch) + ", outside the MIDI range 0 to 127.");
    if (velocity < 1 || velocity > 127)
        return fail(Error::NoteOutOfRange, noteText + " has velocity " + juce::String(velocity) +
                                               ", outside the range 1 to 127 (0 is not a note-on).");
    if (channel < 1 || channel > 16)
        return fail(Error::NoteOutOfRange,
                    noteText + " is on channel " + juce::String(channel) + ", outside the MIDI range 1 to 16.");

    return {};
}

TimelineValidationResult validateClip(const juce::var& clipVar, const juce::String& trackText,
                                      std::set<std::int64_t>& seenClipIds, std::set<std::int64_t>& seenNoteIds,
                                      std::int64_t& totalNotes) {
    auto* cObj = clipVar.getDynamicObject();
    if (cObj == nullptr)
        return fail(Error::MalformedRoot, "A clip on " + trackText + " is not an object.");

    std::int64_t clipId = 0;
    if (!readId(cObj->getProperty("id"), clipId))
        return fail(Error::MalformedRoot, "A clip on " + trackText + " is missing a positive integer \"id\".");
    if (!seenClipIds.insert(clipId).second)
        return fail(Error::MalformedRoot, "Duplicate clip id " + juce::String(clipId) +
                                              ". Clip ids must be unique across the whole timeline.");

    juce::String name;
    if (!readOptionalString(cObj->getProperty("name"), name))
        return fail(Error::MalformedRoot, "Clip " + juce::String(clipId) + " has a non-string \"name\".");
    const juce::String clipText = clipLabel(clipId, name);

    double startBeat = 0.0;
    double lengthBeats = 4.0;
    if (!readOptionalDouble(cObj->getProperty("startBeat"), startBeat) ||
        !readOptionalDouble(cObj->getProperty("lengthBeats"), lengthBeats))
        return fail(Error::MalformedRoot, clipText + " has a non-numeric \"startBeat\" or \"lengthBeats\".");
    if (!isBeatInBounds(startBeat))
        return fail(Error::BeatOutOfBounds, clipText + " starts at beat " + juce::String(startBeat) +
                                                ", which is not a finite beat between " + beatRangeText() + ".");
    if (!isLengthInBounds(lengthBeats))
        return fail(Error::BeatOutOfBounds, clipText + " is " + juce::String(lengthBeats) +
                                                " beats long. A clip length must be finite, greater than 0 and at "
                                                "most " +
                                                juce::String(kMaxPpqUntrusted) + ".");

    // Assets stay trusted-only forever. TimelineDoc's own rule (isValidAssetRef) is about a path
    // ESCAPING the bundle; this one is stricter and simpler — untrusted input may not name an
    // asset at all, however well-formed the reference is. Same class of rule as a node's "state"
    // object on the patch side (AIStateMapper::applyExtraStateToProcessor).
    juce::String assetRef;
    if (!readOptionalString(cObj->getProperty("assetRef"), assetRef))
        return fail(Error::MalformedRoot, clipText + " has a non-string \"assetRef\".");
    if (assetRef.isNotEmpty())
        return fail(Error::AssetNotAllowed, clipText + " names the audio asset \"" + assetRef +
                                                "\". Audio assets and the clips that reference them are created by "
                                                "the app (recording, import), never supplied here — send a MIDI clip "
                                                "with an empty \"assetRef\" instead.");

    double gainDb = 0.0;
    double fadeInBeats = 0.0;
    double fadeOutBeats = 0.0;
    double sourceStartSeconds = 0.0;
    if (!readOptionalDouble(cObj->getProperty("gainDb"), gainDb) ||
        !readOptionalDouble(cObj->getProperty("fadeInBeats"), fadeInBeats) ||
        !readOptionalDouble(cObj->getProperty("fadeOutBeats"), fadeOutBeats) ||
        !readOptionalDouble(cObj->getProperty("sourceStartSeconds"), sourceStartSeconds))
        return fail(Error::MalformedRoot, clipText + " has a non-numeric audio field (\"gainDb\", \"fadeInBeats\", "
                                                     "\"fadeOutBeats\" or \"sourceStartSeconds\").");
    if (!std::isfinite(gainDb))
        return fail(Error::MalformedRoot, clipText + " has a non-finite \"gainDb\".");
    if (!std::isfinite(sourceStartSeconds) || sourceStartSeconds < 0.0)
        return fail(Error::MalformedRoot, clipText + " has a negative or non-finite \"sourceStartSeconds\".");
    if (!isBeatInBounds(fadeInBeats) || !isBeatInBounds(fadeOutBeats))
        return fail(Error::BeatOutOfBounds, clipText +
                                                " has a fade length that is not a finite number of beats "
                                                "between " +
                                                beatRangeText() + ".");

    const juce::Array<juce::var>* noteList = nullptr;
    if (!readOptionalArray(cObj->getProperty("notes"), noteList))
        return fail(Error::MalformedRoot, clipText + " has a \"notes\" property that is not an array.");
    if (noteList != nullptr) {
        if (noteList->size() > TimelineDoc::kMaxNotesPerClip)
            return fail(Error::TooManyNotes, clipText + " has " + juce::String(noteList->size()) +
                                                 " notes, exceeding the limit of " +
                                                 juce::String(TimelineDoc::kMaxNotesPerClip) + " per clip.");
        totalNotes += noteList->size();

        for (const auto& noteVar : *noteList) {
            const auto result = validateNote(noteVar, clipText, seenNoteIds);
            if (!result.ok)
                return result;
        }
    }

    return {};
}

TimelineValidationResult validateLane(const juce::var& laneVar, const juce::String& trackText,
                                      const std::map<juce::String, juce::AudioProcessor*>& graphByUuid,
                                      std::set<std::int64_t>& seenLaneIds,
                                      std::set<std::pair<juce::String, juce::String>>& seenLaneParams) {
    auto* lObj = laneVar.getDynamicObject();
    if (lObj == nullptr)
        return fail(Error::MalformedRoot, "An automation lane on " + trackText + " is not an object.");

    std::int64_t laneId = 0;
    if (!readId(lObj->getProperty("id"), laneId))
        return fail(Error::MalformedRoot,
                    "An automation lane on " + trackText + " is missing a positive integer \"id\".");
    if (!seenLaneIds.insert(laneId).second)
        return fail(Error::MalformedRoot, "Duplicate automation lane id " + juce::String(laneId) +
                                              ". Lane ids must be unique across the whole timeline.");

    const juce::String laneText = "Automation lane " + juce::String(laneId);

    const juce::var nodeUuidVar = lObj->getProperty("nodeUuid");
    const juce::var paramIdVar = lObj->getProperty("paramId");
    if (!nodeUuidVar.isString() || !paramIdVar.isString())
        return fail(Error::MalformedRoot, laneText + " must carry a string \"nodeUuid\" and a string \"paramId\".");
    const juce::String nodeUuid = nodeUuidVar.toString();
    const juce::String paramId = paramIdVar.toString();
    if (nodeUuid.isEmpty() || paramId.isEmpty())
        return fail(Error::MalformedRoot, laneText + " has an empty \"nodeUuid\" or \"paramId\". A lane is identified "
                                                     "by the parameter it automates, so neither may be blank.");
    // The doc-wide one-lane-per-parameter rule is an invariant of the model, not a preference:
    // two lanes on one parameter have no defined precedence.
    if (!seenLaneParams.insert({nodeUuid, paramId}).second)
        return fail(Error::MalformedRoot, laneText + " automates parameter \"" + paramId + "\" on node \"" + nodeUuid +
                                              "\", which another lane already automates. There is at most one lane "
                                              "per parameter in the whole timeline.");

    // The binding must resolve against the LIVE graph. An orphaned binding is a state the app
    // RECOVERS from when a node disappears under an existing lane (TL2-6); it is not a state
    // untrusted input may author from nothing.
    const auto found = graphByUuid.find(nodeUuid);
    if (found == graphByUuid.end())
        return fail(Error::UnresolvableBinding, laneText + " is bound to node uuid \"" + nodeUuid +
                                                    "\", which no module in the current patch has. Bind the lane to a "
                                                    "module that exists.");
    auto* parameter = findParameter(found->second, paramId);
    if (parameter == nullptr)
        return fail(Error::UnresolvableBinding, laneText + " automates parameter \"" + paramId + "\", which node \"" +
                                                    nodeUuid +
                                                    "\" does not have. Use one of that module's parameter "
                                                    "ids.");

    // The lane's own RangeSnapshot is checked for internal consistency (the loader requires it),
    // but is NEVER used to bound a value: it is data the model wrote, and a model that widened it
    // would be authorising its own out-of-range points. The live parameter is the truth.
    AutomationLane::RangeSnapshot declared;
    const juce::var rangeVar = lObj->getProperty("range");
    if (auto* rObj = rangeVar.getDynamicObject()) {
        if (!readOptionalFloat(rObj->getProperty("minValue"), declared.minValue) ||
            !readOptionalFloat(rObj->getProperty("maxValue"), declared.maxValue) ||
            !readOptionalFloat(rObj->getProperty("defaultValue"), declared.defaultValue))
            return fail(Error::MalformedRoot, laneText + " has a non-numeric value in its \"range\" object.");
    } else if (!rangeVar.isVoid()) {
        return fail(Error::MalformedRoot, laneText + " has a \"range\" property that is not an object.");
    }
    if (!std::isfinite(declared.minValue) || !std::isfinite(declared.maxValue) ||
        !std::isfinite(declared.defaultValue) || declared.minValue > declared.maxValue)
        return fail(Error::MalformedRoot, laneText + " declares a \"range\" whose minValue is above its maxValue, or "
                                                     "which is not finite.");

    // Read and Off only. Touch/Latch/Write arm the lane to CAPTURE the user's own gestures, which
    // is a recording decision that belongs to the person at the keyboard.
    int recordMode = static_cast<int>(LaneRecordMode::Read);
    if (!readOptionalInt(lObj->getProperty("recordMode"), recordMode))
        return fail(Error::MalformedRoot, laneText + " has a non-integer \"recordMode\".");
    if (recordMode != static_cast<int>(LaneRecordMode::Off) && recordMode != static_cast<int>(LaneRecordMode::Read))
        return fail(Error::RecordModeNotAllowed,
                    laneText + " asks for record mode " + juce::String(recordMode) +
                        ". Only Read (1) and Off (0) are accepted here — arming a lane to record is a decision the "
                        "user makes in the app.");

    const juce::Array<juce::var>* pointList = nullptr;
    if (!readOptionalArray(lObj->getProperty("points"), pointList))
        return fail(Error::MalformedRoot, laneText + " has a \"points\" property that is not an array.");
    if (pointList == nullptr)
        return {};

    if (pointList->size() > TimelineDoc::kMaxBreakpointsPerLane)
        return fail(Error::TooManyBreakpoints, laneText + " has " + juce::String(pointList->size()) +
                                                   " breakpoints, exceeding the limit of " +
                                                   juce::String(TimelineDoc::kMaxBreakpointsPerLane) + " per lane.");

    const auto& liveRange = parameter->getNormalisableRange();
    const double liveMin = static_cast<double>(liveRange.start);
    const double liveMax = static_cast<double>(liveRange.end);

    for (const auto& pointVar : *pointList) {
        auto* pObj = pointVar.getDynamicObject();
        if (pObj == nullptr)
            return fail(Error::MalformedRoot, "A breakpoint on " + laneText + " is not an object.");

        double beat = 0.0;
        double value = 0.0;
        float tension = 0.0f;
        int curve = static_cast<int>(BreakpointCurve::Linear);
        if (!readOptionalDouble(pObj->getProperty("beat"), beat) ||
            !readOptionalDouble(pObj->getProperty("value"), value))
            return fail(Error::MalformedRoot, "A breakpoint on " + laneText +
                                                  " has a non-numeric \"beat\" or "
                                                  "\"value\".");
        if (!isBeatInBounds(beat))
            return fail(Error::BeatOutOfBounds, "A breakpoint on " + laneText + " sits at beat " + juce::String(beat) +
                                                    ", which is not a finite beat between " + beatRangeText() + ".");
        if (!std::isfinite(value))
            return fail(Error::MalformedRoot, "The breakpoint at beat " + juce::String(beat) + " on " + laneText +
                                                  " has a non-finite value.");
        if (value < liveMin || value > liveMax)
            return fail(Error::ValueOutOfParamRange, "The breakpoint at beat " + juce::String(beat) + " on " +
                                                         laneText + " has value " + juce::String(value) +
                                                         ", outside the range " + juce::String(liveMin) + " to " +
                                                         juce::String(liveMax) + " of parameter \"" + paramId +
                                                         "\". Automation values are in the "
                                                         "parameter's own units.");

        // Rejected rather than clamped, unlike addBreakpoint/fromVar — see the note on notes above.
        if (!readOptionalFloat(pObj->getProperty("tension"), tension))
            return fail(Error::MalformedRoot, "The breakpoint at beat " + juce::String(beat) + " on " + laneText +
                                                  " has a non-numeric \"tension\".");
        if (tension < -1.0f || tension > 1.0f)
            return fail(Error::MalformedRoot, "The breakpoint at beat " + juce::String(beat) + " on " + laneText +
                                                  " has tension " + juce::String(tension) + ", outside -1 to 1.");
        if (!readOptionalInt(pObj->getProperty("curve"), curve))
            return fail(Error::MalformedRoot, "The breakpoint at beat " + juce::String(beat) + " on " + laneText +
                                                  " has a non-integer \"curve\".");
        if (curve < static_cast<int>(BreakpointCurve::Hold) || curve > static_cast<int>(BreakpointCurve::Bezier))
            return fail(Error::MalformedRoot, "The breakpoint at beat " + juce::String(beat) + " on " + laneText +
                                                  " uses curve " + juce::String(curve) +
                                                  ". Use 0 (hold), 1 (linear) or 2 (bezier).");
    }

    return {};
}

TimelineValidationResult validateTrack(const juce::var& trackVar,
                                       const std::map<juce::String, juce::AudioProcessor*>& graphByUuid,
                                       std::set<std::int64_t>& seenTrackIds, std::set<std::int64_t>& seenClipIds,
                                       std::set<std::int64_t>& seenNoteIds, std::set<std::int64_t>& seenLaneIds,
                                       std::set<std::pair<juce::String, juce::String>>& seenLaneParams,
                                       std::int64_t& totalNotes) {
    auto* tObj = trackVar.getDynamicObject();
    if (tObj == nullptr)
        return fail(Error::MalformedRoot, "A track entry is not an object.");

    std::int64_t trackId = 0;
    if (!readId(tObj->getProperty("id"), trackId))
        return fail(Error::MalformedRoot, "A track is missing a positive integer \"id\".");
    if (!seenTrackIds.insert(trackId).second)
        return fail(Error::MalformedRoot,
                    "Duplicate track id " + juce::String(trackId) + ". Track ids must be unique.");

    juce::String name;
    if (!readOptionalString(tObj->getProperty("name"), name))
        return fail(Error::MalformedRoot, "Track " + juce::String(trackId) + " has a non-string \"name\".");
    const juce::String trackText = trackLabel(trackId, name);

    // Kinds 3..15 are reserved for a future build; refuse one rather than coerce it into a kind
    // this build understands. Audio (1) is allowed as a KIND — what makes an audio track
    // unauthorable in practice is the per-clip assetRef refusal above, since an audio track with
    // no asset-bearing clip is just an empty row.
    int kind = static_cast<int>(TrackKind::Midi);
    if (!readOptionalInt(tObj->getProperty("kind"), kind))
        return fail(Error::MalformedRoot, trackText + " has a non-integer \"kind\".");
    if (kind < static_cast<int>(TrackKind::Midi) || kind > static_cast<int>(TrackKind::Automation))
        return fail(Error::ReservedKindNotAllowed,
                    trackText + " has kind " + juce::String(kind) +
                        ", which is reserved for a future format. Use 0 (MIDI), 1 (audio) or 2 (automation).");

    std::int64_t colourArgb = 0xff808080;
    bool muted = false;
    bool soloed = false;
    bool armed = false;
    juce::String bindingUuid;
    if (!readOptionalInt64(tObj->getProperty("colourArgb"), colourArgb) ||
        !readOptionalBool(tObj->getProperty("muted"), muted) ||
        !readOptionalBool(tObj->getProperty("soloed"), soloed) ||
        !readOptionalBool(tObj->getProperty("armed"), armed) ||
        !readOptionalString(tObj->getProperty("bindingUuid"), bindingUuid))
        return fail(Error::MalformedRoot, trackText + " has a mistyped \"colourArgb\", \"muted\", \"soloed\", "
                                                      "\"armed\" or \"bindingUuid\".");
    if (colourArgb < 0 || colourArgb > 0xffffffffLL)
        return fail(Error::MalformedRoot,
                    trackText + " has a \"colourArgb\" outside the 32-bit range 0 to 4294967295.");

    // Same rule as a lane's binding: an EMPTY bindingUuid is legal (an unbound track plays
    // nowhere), a non-empty one must name a live node.
    if (bindingUuid.isNotEmpty() && graphByUuid.count(bindingUuid) == 0)
        return fail(Error::UnresolvableBinding, trackText + " is bound to node uuid \"" + bindingUuid +
                                                    "\", which no module in the current patch has. Leave "
                                                    "\"bindingUuid\" empty for an unbound track.");

    const juce::Array<juce::var>* clipList = nullptr;
    if (!readOptionalArray(tObj->getProperty("clips"), clipList))
        return fail(Error::MalformedRoot, trackText + " has a \"clips\" property that is not an array.");
    if (clipList != nullptr) {
        if (clipList->size() > TimelineDoc::kMaxClipsPerTrack)
            return fail(Error::TooManyClips, trackText + " has " + juce::String(clipList->size()) +
                                                 " clips, exceeding the limit of " +
                                                 juce::String(TimelineDoc::kMaxClipsPerTrack) + " per track.");
        for (const auto& clipVar : *clipList) {
            const auto result = validateClip(clipVar, trackText, seenClipIds, seenNoteIds, totalNotes);
            if (!result.ok)
                return result;
        }
    }

    const juce::Array<juce::var>* laneList = nullptr;
    if (!readOptionalArray(tObj->getProperty("lanes"), laneList))
        return fail(Error::MalformedRoot, trackText + " has a \"lanes\" property that is not an array.");
    if (laneList != nullptr) {
        if (laneList->size() > TimelineDoc::kMaxLanesPerTrack)
            return fail(Error::TooManyLanes, trackText + " has " + juce::String(laneList->size()) +
                                                 " automation lanes, exceeding the limit of " +
                                                 juce::String(TimelineDoc::kMaxLanesPerTrack) + " per track.");
        for (const auto& laneVar : *laneList) {
            const auto result = validateLane(laneVar, trackText, graphByUuid, seenLaneIds, seenLaneParams);
            if (!result.ok)
                return result;
        }
    }

    return {};
}

} // namespace

juce::String timelineValidationErrorName(TimelineValidationError error) {
    switch (error) {
    case TimelineValidationError::None:
        return "None";
    case TimelineValidationError::MalformedRoot:
        return "MalformedRoot";
    case TimelineValidationError::TooManyTracks:
        return "TooManyTracks";
    case TimelineValidationError::TooManyClips:
        return "TooManyClips";
    case TimelineValidationError::TooManyNotes:
        return "TooManyNotes";
    case TimelineValidationError::TooManyLanes:
        return "TooManyLanes";
    case TimelineValidationError::TooManyBreakpoints:
        return "TooManyBreakpoints";
    case TimelineValidationError::BeatOutOfBounds:
        return "BeatOutOfBounds";
    case TimelineValidationError::NoteOutOfRange:
        return "NoteOutOfRange";
    case TimelineValidationError::ValueOutOfParamRange:
        return "ValueOutOfParamRange";
    case TimelineValidationError::UnresolvableBinding:
        return "UnresolvableBinding";
    case TimelineValidationError::AssetNotAllowed:
        return "AssetNotAllowed";
    case TimelineValidationError::RecordModeNotAllowed:
        return "RecordModeNotAllowed";
    case TimelineValidationError::ReservedKindNotAllowed:
        return "ReservedKindNotAllowed";
    case TimelineValidationError::InternalError:
        return "InternalError";
    }
    return "Unknown";
}

TimelineValidationResult validateTimeline(const juce::var& timelineVar, const juce::AudioProcessorGraph& graph) {
    auto* rootObj = timelineVar.getDynamicObject();
    if (rootObj == nullptr)
        return fail(Error::MalformedRoot, "Timeline data must be a JSON object with a \"version\" and a \"tracks\" "
                                          "array.");

    int version = 0;
    if (!readInt(rootObj->getProperty("version"), version))
        return fail(Error::MalformedRoot, "Timeline data is missing an integer \"version\" field.");
    // A FUTURE version is the one that matters: it describes a document this build cannot reason
    // about, so every check below would be inspecting a shape it does not know. (The loader
    // additionally demands an exact match, which today is the same thing — kFormatVersion is 1.)
    if (version < 1 || version > TimelineDoc::kFormatVersion)
        return fail(Error::MalformedRoot, "Timeline \"version\" " + juce::String(version) +
                                              " is not a format this build accepts (expected 1 to " +
                                              juce::String(TimelineDoc::kFormatVersion) + ").");

    // Unknown top-level keys are REFUSED, where PatchDocument deliberately PRESERVES the ones it
    // does not understand (so a project saved by a newer build survives a round trip through an
    // older one). That asymmetry is the point: forward-compatibility is a property a document
    // format needs and an untrusted payload does not. An ignored key is precisely how a later
    // build starts honouring a field that today's gate never inspected — the failure TL0-4's
    // "timeline" refusal exists to prevent, one level down.
    for (int i = 0; i < rootObj->getProperties().size(); ++i) {
        const juce::String key = rootObj->getProperties().getName(i).toString();
        if (key != "version" && key != "tracks" && key != "nextTrackId" && key != "nextClipId" && key != "nextLaneId" &&
            key != "nextNoteId")
            return fail(Error::MalformedRoot,
                        "Unknown top-level timeline key \"" + key +
                            "\". Only \"version\", \"tracks\" and the next-id counters are accepted.");
    }

    const auto graphByUuid = indexGraphByUuid(graph);

    const juce::Array<juce::var>* trackList = nullptr;
    if (!readOptionalArray(rootObj->getProperty("tracks"), trackList))
        return fail(Error::MalformedRoot, "Timeline \"tracks\" must be an array.");

    std::set<std::int64_t> seenTrackIds;
    std::set<std::int64_t> seenClipIds;
    std::set<std::int64_t> seenNoteIds;
    std::set<std::int64_t> seenLaneIds;
    std::set<std::pair<juce::String, juce::String>> seenLaneParams;
    std::int64_t totalNotes = 0;

    if (trackList != nullptr) {
        if (trackList->size() > TimelineDoc::kMaxTracks)
            return fail(Error::TooManyTracks, "Timeline has " + juce::String(trackList->size()) +
                                                  " tracks, exceeding the limit of " +
                                                  juce::String(TimelineDoc::kMaxTracks) + ".");

        for (const auto& trackVar : *trackList) {
            const auto result = validateTrack(trackVar, graphByUuid, seenTrackIds, seenClipIds, seenNoteIds,
                                              seenLaneIds, seenLaneParams, totalNotes);
            if (!result.ok)
                return result;
        }
    }

    // The whole-document note budget, checked after the per-clip caps so the more specific
    // message wins when a single clip is the culprit.
    if (totalNotes > kMaxTotalNotesUntrusted)
        return fail(Error::TooManyNotes, "Timeline has " + juce::String(totalNotes) +
                                             " notes across all clips, exceeding the limit of " +
                                             juce::String(kMaxTotalNotesUntrusted) + ".");

    // Belt and braces: prove the document actually loads, into a doc that is thrown away. This is
    // what lets the caller treat a pass as "fromVar will accept this", and it is the only source
    // of InternalError — a var that satisfied every named check above and was still refused means
    // this validator and TimelineDoc::fromVar have drifted apart. Today the one reachable way in
    // is a malformed next-id counter, which is the document's own bookkeeping and not something a
    // tool payload has any reason to carry.
    TimelineDoc scratch;
    if (!scratch.fromVar(timelineVar))
        return fail(Error::InternalError,
                    "The timeline passed every validation check but was still refused by the loader, which means it "
                    "carries something this gate does not model (a malformed \"nextTrackId\"/\"nextClipId\"/"
                    "\"nextLaneId\"/\"nextNoteId\" counter is the known case). It has not been applied.");

    return {};
}

} // namespace synth
