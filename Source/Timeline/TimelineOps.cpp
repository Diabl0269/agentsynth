#include "TimelineOps.h"

#include "../AppUndoManager.h"
#include "AutomationBinding.h"
#include "MidiClipFile.h"
#include "TimelineValidator.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <set>
#include <vector>

namespace synth {

namespace {

TimelineOpsResult fail(const juce::String& message) { return {false, message, {}}; }

// -- juce::var readers ------------------------------------------------------------------------
// Deliberately the same shape as TimelineValidator.cpp's readers — an ABSENT property takes the
// field's default, a PRESENT one must be well-typed. Restated here rather than shared because
// those are file-local helpers by design; what this file imports from that one is the part that
// matters, the RULES (kMaxPpqUntrusted, kMaxTotalNotesUntrusted, and TimelineDoc's own caps).

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

// Accepts ints too: a JSON writer is free to emit 4 rather than 4.0 for a whole-numbered beat.
bool readDouble(const juce::var& v, double& out) {
    if (v.isDouble() || v.isInt() || v.isInt64()) {
        out = static_cast<double>(v);
        return true;
    }
    return false;
}

bool readOptionalInt(const juce::var& v, int& out) { return v.isVoid() || readInt(v, out); }
bool readOptionalDouble(const juce::var& v, double& out) { return v.isVoid() || readDouble(v, out); }

bool readOptionalFloat(const juce::var& v, float& out) {
    if (v.isVoid())
        return true;
    double asDouble = 0.0;
    if (!readDouble(v, asDouble) || !std::isfinite(asDouble))
        return false;
    out = static_cast<float>(asDouble);
    return true;
}

bool isBeatInBounds(double v) { return std::isfinite(v) && v >= 0.0 && v <= kMaxPpqUntrusted; }
bool isLengthInBounds(double v) { return std::isfinite(v) && v > 0.0 && v <= kMaxPpqUntrusted; }

juce::String beatRangeText() { return "0 and " + juce::String(kMaxPpqUntrusted); }

// -- closed objects ---------------------------------------------------------------------------

/** The first key of `o` that is not in `allowed`, or "" when every key is.
 *
 *  Every op — and every clip, note and point inside one — is a CLOSED object: an unrecognised key
 *  is refused, not ignored. That is what keeps audio assets, lane record arming and track bindings
 *  unreachable by GRAMMAR rather than by a list of field-by-field refusals that a later build could
 *  quietly stop applying. Same reasoning as validateTimeline's unknown-top-level-key rule; the
 *  envelope ROOT is the one place unknown keys are fine, because that is where the sibling patch's
 *  own "nodes"/"connections"/"mode" live. */
juce::String unknownKey(juce::DynamicObject& o, std::initializer_list<const char*> allowed) {
    for (int i = 0; i < o.getProperties().size(); ++i) {
        const juce::String key = o.getProperties().getName(i).toString();
        bool known = false;
        for (const char* candidate : allowed) {
            if (key == candidate) {
                known = true;
                break;
            }
        }
        if (!known)
            return key;
    }
    return {};
}

// -- preview text -----------------------------------------------------------------------------
// Every helper here is deterministic: the same envelope against the same document must always
// produce the same summary, because that string is what the user reads before agreeing to the
// edit and what TimelineOpsTests pins.

/** A beat as the shortest exact-looking decimal: "4" not "4.0", "4.5" not "4.500". */
juce::String beatText(double v) {
    if (std::isfinite(v) && v == std::floor(v) && std::abs(v) < 1.0e15)
        // juce::int64, not std::int64_t: juce::String has no `long` constructor, so an LP64
        // int64_t argument is ambiguous between the int and int64 constructors on Linux.
        return juce::String(static_cast<juce::int64>(v));
    return juce::String(v, 3).trimCharactersAtEnd("0");
}

juce::String countText(int n, const juce::String& singular, const juce::String& plural) {
    return juce::String(n) + " " + (n == 1 ? singular : plural);
}

/** Clip windows are listed at most this many times before the tail is elided — a placeClips op may
 *  legally carry thousands, and the preview is a chat card, not a manifest. */
constexpr int kMaxPreviewedClipWindows = 4;

// -- graph lookups ----------------------------------------------------------------------------
// A lane binds to a node's "uuid" PROPERTY, never its integer NodeID (merge-mode graph apply is
// free to renumber those) — the same key TimelineReconciler and validateTimeline resolve on.

juce::AudioProcessor* findProcessorByUuid(const juce::AudioProcessorGraph& graph, const juce::String& uuid) {
    if (uuid.isEmpty())
        return nullptr;
    for (auto* node : graph.getNodes())
        if (node != nullptr && node->properties["uuid"].toString() == uuid)
            return node->getProcessor();
    return nullptr;
}

// Parameter resolution itself (exact id match, or the narrow hosted-plugin index fallback)
// is factored into synth::resolveLaneParameter (AutomationBinding.h) — the ONE resolver this file,
// TimelineValidator, AudioEngine::publishTimeline's binding build and the recorder's rebind all
// share, so "does this lane's parameter resolve" cannot drift between the AI-tool path and the
// audio path.

// -- name check -------------------------------------------------------------------------------

TimelineOpsResult checkName(const juce::String& where, const juce::var& nameVar, const juce::String& field,
                            bool required, juce::String& out) {
    if (nameVar.isVoid()) {
        if (required)
            return fail(where + "needs a string \"" + field + "\".");
        return {};
    }
    if (!nameVar.isString())
        return fail(where + "has a non-string \"" + field + "\".");
    out = nameVar.toString();
    if (required && out.isEmpty())
        return fail(where + "has an empty \"" + field +
                    "\". A track is addressed by name in later ops, so it must have one.");
    if (out.length() > TimelineOps::kMaxNameChars)
        return fail(where + "has a \"" + field + "\" of " + juce::String(out.length()) +
                    " characters, exceeding the limit of " + juce::String(TimelineOps::kMaxNameChars) + ".");
    return {};
}

// -- addTrack ---------------------------------------------------------------------------------

TimelineOpsResult runAddTrack(const juce::String& where, juce::DynamicObject& op, TimelineDoc& doc,
                              juce::StringArray& parts) {
    if (const auto bad = unknownKey(op, {"op", "kind", "name"}); bad.isNotEmpty())
        return fail(where + "has an unknown field \"" + bad + "\". An addTrack op accepts only \"kind\" and \"name\".");

    const juce::var kindVar = op.getProperty("kind");
    if (!kindVar.isString())
        return fail(where + "needs a string \"kind\", either \"midi\" or \"automation\".");
    const juce::String kindText = kindVar.toString();

    // "audio" is deliberately not offered. An audio track carries clips that reference recorded or
    // imported assets, and assets are trusted-only forever (validateTimeline's AssetNotAllowed) —
    // an audio track authored here could only ever be an empty row.
    TrackKind kind = TrackKind::Midi;
    if (kindText == "midi")
        kind = TrackKind::Midi;
    else if (kindText == "automation")
        kind = TrackKind::Automation;
    else
        return fail(where + "asks for kind \"" + kindText +
                    "\". Only \"midi\" and \"automation\" are accepted - an audio track needs a recorded or "
                    "imported asset, which is created by the app and never supplied here.");

    juce::String name;
    if (const auto result = checkName(where, op.getProperty("name"), "name", /*required=*/true, name); !result.ok)
        return result;

    if (static_cast<int>(doc.getTracks().size()) >= TimelineDoc::kMaxTracks)
        return fail(where + "would take the timeline past its limit of " + juce::String(TimelineDoc::kMaxTracks) +
                    " tracks.");

    const TrackId id = doc.addTrack(kind, name);
    if (!id.isValid())
        return fail(where + "could not create the track.");

    // addTrack creates the DOC track only — no graph node, no Track In wiring. Deciding which
    // module a track plays through is a routing decision about the user's own patch, so it stays a
    // user/host gesture, and the preview says so rather than leaving them to discover it.
    // ASCII only, here and everywhere else this file builds a string: juce::String's narrow
    // const char* constructor takes ASCII, not UTF-8, so an em dash in a literal would reach the
    // chat card as mojibake rather than as a dash.
    parts.add("adds " + kindText + " track \"" + name + "\"" +
              (kind == TrackKind::Midi ? juce::String(" (unbound - bind it in the timeline panel)") : juce::String()));
    return {};
}

// -- placeClips -------------------------------------------------------------------------------

TimelineOpsResult resolveTrack(const juce::String& where, const juce::var& selector, const TimelineDoc& doc,
                               TrackId& idOut, juce::String& nameOut) {
    const int trackCount = static_cast<int>(doc.getTracks().size());

    if (selector.isString()) {
        const juce::String wanted = selector.toString();
        if (wanted.isEmpty())
            return fail(where + "names an empty \"track\".");

        // Ambiguity is REJECTED, never resolved by picking the first: two tracks called "Bass" mean
        // the sender has no idea which one they addressed, and guessing writes music onto a track
        // nobody asked for.
        int matches = 0;
        for (const auto& track : doc.getTracks()) {
            if (track.name == wanted) {
                ++matches;
                idOut = track.id;
            }
        }
        if (matches == 0)
            return fail(where + "targets track \"" + wanted +
                        "\", which the timeline does not have. Add it first with an addTrack op, or target an "
                        "existing track by its exact name.");
        if (matches > 1)
            return fail(where + "targets track \"" + wanted + "\", but " + juce::String(matches) +
                        " tracks have that name. Target it by { \"index\": N } instead.");
        nameOut = wanted;
        return {};
    }

    if (auto* selectorObj = selector.getDynamicObject()) {
        if (const auto bad = unknownKey(*selectorObj, {"index"}); bad.isNotEmpty())
            return fail(where + "has an unknown field \"" + bad +
                        "\" in its \"track\" selector, which accepts only \"index\".");
        int index = -1;
        if (!readInt(selectorObj->getProperty("index"), index))
            return fail(where + "has a \"track\" selector with no integer \"index\".");
        if (index < 0 || index >= trackCount)
            return fail(where + "targets track index " + juce::String(index) + ", but the timeline has " +
                        juce::String(trackCount) + " tracks.");
        idOut = doc.getTracks()[static_cast<size_t>(index)].id;
        nameOut = doc.getTracks()[static_cast<size_t>(index)].name;
        return {};
    }

    return fail(where + "needs a \"track\", either the track's exact name or { \"index\": N }.");
}

TimelineOpsResult readNote(const juce::String& where, const juce::var& noteVar, MidiNote& out) {
    auto* noteObj = noteVar.getDynamicObject();
    if (noteObj == nullptr)
        return fail(where + "has a note that is not an object.");
    if (const auto bad = unknownKey(*noteObj, {"startBeat", "lengthBeats", "pitch", "velocity", "channel"});
        bad.isNotEmpty())
        return fail(where + "has a note with an unknown field \"" + bad +
                    "\". A note accepts only \"startBeat\", \"lengthBeats\", \"pitch\", \"velocity\" and "
                    "\"channel\".");

    if (!readOptionalDouble(noteObj->getProperty("startBeat"), out.startBeat) ||
        !readOptionalDouble(noteObj->getProperty("lengthBeats"), out.lengthBeats))
        return fail(where + "has a note with a non-numeric \"startBeat\" or \"lengthBeats\".");
    if (!isBeatInBounds(out.startBeat))
        return fail(where + "has a note starting at beat " + juce::String(out.startBeat) +
                    ", which is not a finite beat between " + beatRangeText() +
                    ". Note beats are relative to the clip's own start.");
    if (!isLengthInBounds(out.lengthBeats))
        return fail(where + "has a note " + juce::String(out.lengthBeats) +
                    " beats long. A note length must be finite, greater than 0 and at most " +
                    juce::String(kMaxPpqUntrusted) + ".");

    // Ranges are REJECTED, never clamped — validateTimeline's rule, for its reason: a hand on a
    // control means "as far as this goes", but a sender naming pitch 200 means something the
    // parameter cannot express, and quietly rewriting it to 127 ships music nobody asked for.
    if (!readOptionalInt(noteObj->getProperty("pitch"), out.pitch) ||
        !readOptionalInt(noteObj->getProperty("velocity"), out.velocity) ||
        !readOptionalInt(noteObj->getProperty("channel"), out.channel))
        return fail(where + "has a note with a non-integer \"pitch\", \"velocity\" or \"channel\".");
    if (out.pitch < 0 || out.pitch > 127)
        return fail(where + "has a note with pitch " + juce::String(out.pitch) + ", outside the MIDI range 0 to 127.");
    if (out.velocity < 1 || out.velocity > 127)
        return fail(where + "has a note with velocity " + juce::String(out.velocity) +
                    ", outside the range 1 to 127 (0 is not a note-on).");
    if (out.channel < 1 || out.channel > 16)
        return fail(where + "has a note on channel " + juce::String(out.channel) + ", outside the MIDI range 1 to 16.");
    return {};
}

TimelineOpsResult runPlaceClips(const juce::String& where, juce::DynamicObject& op, TimelineDoc& doc,
                                juce::StringArray& parts, std::int64_t& totalNotes) {
    if (const auto bad = unknownKey(op, {"op", "track", "clips"}); bad.isNotEmpty())
        return fail(where + "has an unknown field \"" + bad +
                    "\". A placeClips op accepts only \"track\" and \"clips\".");

    TrackId trackId;
    juce::String trackName;
    if (const auto result = resolveTrack(where, op.getProperty("track"), doc, trackId, trackName); !result.ok)
        return result;

    const Track* track = doc.getTrack(trackId);
    if (track == nullptr)
        return fail(where + "could not resolve its target track.");
    if (track->kind != TrackKind::Midi)
        return fail(where + "targets track \"" + trackName +
                    "\", which is not a MIDI track. Clips carrying notes belong on a MIDI track.");

    const juce::var clipsVar = op.getProperty("clips");
    if (!clipsVar.isArray())
        return fail(where + "needs a \"clips\" array.");
    const auto& clipList = *clipsVar.getArray();
    if (clipList.isEmpty())
        return fail(where + "has an empty \"clips\" array - there is nothing to place.");

    const int existingClips = static_cast<int>(track->clips.size());
    if (existingClips + clipList.size() > TimelineDoc::kMaxClipsPerTrack)
        return fail(where + "would put " + juce::String(existingClips + clipList.size()) + " clips on track \"" +
                    trackName + "\", exceeding the limit of " + juce::String(TimelineDoc::kMaxClipsPerTrack) +
                    " per track.");

    // Everything is READ AND CHECKED before a single clip is created, so an op is never half
    // applied — the batch's all-or-nothing promise has to hold inside an op as well as across one.
    struct PendingClip {
        double startBeat = 0.0;
        double lengthBeats = 4.0;
        juce::String name;
        std::vector<MidiNote> notes;
    };
    std::vector<PendingClip> pending;
    pending.reserve(static_cast<size_t>(clipList.size()));

    for (const auto& clipVar : clipList) {
        auto* clipObj = clipVar.getDynamicObject();
        if (clipObj == nullptr)
            return fail(where + "has a clip that is not an object.");
        if (const auto bad = unknownKey(*clipObj, {"startBeat", "lengthBeats", "name", "notes"}); bad.isNotEmpty())
            return fail(where + "has a clip with an unknown field \"" + bad +
                        "\". A clip accepts only \"startBeat\", \"lengthBeats\", \"name\" and \"notes\" - an "
                        "audio asset is never supplied here.");

        PendingClip clip;
        if (!readOptionalDouble(clipObj->getProperty("startBeat"), clip.startBeat) ||
            !readOptionalDouble(clipObj->getProperty("lengthBeats"), clip.lengthBeats))
            return fail(where + "has a clip with a non-numeric \"startBeat\" or \"lengthBeats\".");
        if (!isBeatInBounds(clip.startBeat))
            return fail(where + "has a clip starting at beat " + juce::String(clip.startBeat) +
                        ", which is not a finite beat between " + beatRangeText() + ".");
        if (!isLengthInBounds(clip.lengthBeats))
            return fail(where + "has a clip " + juce::String(clip.lengthBeats) +
                        " beats long. A clip length must be finite, greater than 0 and at most " +
                        juce::String(kMaxPpqUntrusted) + ".");

        if (const auto result = checkName(where, clipObj->getProperty("name"), "name", /*required=*/false, clip.name);
            !result.ok)
            return result;

        const juce::var notesVar = clipObj->getProperty("notes");
        if (!notesVar.isVoid()) {
            if (!notesVar.isArray())
                return fail(where + "has a clip whose \"notes\" is not an array.");
            const auto& noteList = *notesVar.getArray();
            if (noteList.size() > TimelineDoc::kMaxNotesPerClip)
                return fail(where + "has a clip with " + juce::String(noteList.size()) +
                            " notes, exceeding the limit of " + juce::String(TimelineDoc::kMaxNotesPerClip) +
                            " per clip.");
            totalNotes += noteList.size();
            if (totalNotes > kMaxTotalNotesUntrusted)
                return fail(where + "takes the batch past " + juce::String(kMaxTotalNotesUntrusted) +
                            " notes in total, which is the most one set of timeline operations may author.");

            clip.notes.reserve(static_cast<size_t>(noteList.size()));
            for (const auto& noteVar : noteList) {
                MidiNote note;
                if (const auto result = readNote(where, noteVar, note); !result.ok)
                    return result;
                clip.notes.push_back(note);
            }
        }

        pending.push_back(std::move(clip));
    }

    int placedNotes = 0;
    juce::StringArray windows;
    for (const auto& clip : pending) {
        const ClipId clipId = doc.addClip(trackId, clip.startBeat, clip.lengthBeats, clip.name);
        if (!clipId.isValid())
            return fail(where + "could not create a clip on track \"" + trackName + "\".");
        for (const auto& note : clip.notes) {
            if (!doc.addNote(clipId, note).isValid())
                return fail(where + "could not add a note to a clip on track \"" + trackName + "\".");
            ++placedNotes;
        }
        if (windows.size() < kMaxPreviewedClipWindows)
            windows.add(beatText(clip.startBeat) + "-" + beatText(clip.startBeat + clip.lengthBeats));
    }

    juce::String windowText = windows.joinIntoString(", ");
    if (static_cast<int>(pending.size()) > kMaxPreviewedClipWindows)
        windowText << ", ...";

    parts.add("places " + countText(static_cast<int>(pending.size()), "clip", "clips") + " (" +
              (placedNotes == 0 ? juce::String("no notes") : countText(placedNotes, "note", "notes")) + ") at " +
              windowText + " on \"" + trackName + "\"");
    return {};
}

// -- placeMidiClip ------------------------------------------------------------------------------
// The .mid blob surface: a base64-encoded Standard MIDI File is the one binary payload
// this grammar accepts, because MidiClipFile::importFromStream can only ever decode it to notes —
// no path, no plugin id, no code. Bounds-checking-strict end to end, matching that class's own
// stated design (see MidiClipFile.h's class comment).

TimelineOpsResult runPlaceMidiClip(const juce::String& where, juce::DynamicObject& op, TimelineDoc& doc,
                                   juce::StringArray& parts, std::int64_t& totalNotes) {
    if (const auto bad = unknownKey(op, {"op", "track", "startBeat", "midBase64"}); bad.isNotEmpty())
        return fail(where + "has an unknown field \"" + bad +
                    "\". A placeMidiClip op accepts only \"track\", \"startBeat\" and \"midBase64\".");

    TrackId trackId;
    juce::String trackName;
    if (const auto result = resolveTrack(where, op.getProperty("track"), doc, trackId, trackName); !result.ok)
        return result;

    const Track* track = doc.getTrack(trackId);
    if (track == nullptr)
        return fail(where + "could not resolve its target track.");
    if (track->kind != TrackKind::Midi)
        return fail(where + "targets track \"" + trackName +
                    "\", which is not a MIDI track. A .mid blob's notes belong on a MIDI track.");

    double startBeat = 0.0;
    if (!readOptionalDouble(op.getProperty("startBeat"), startBeat))
        return fail(where + "has a non-numeric \"startBeat\".");
    if (!isBeatInBounds(startBeat))
        return fail(where + "starts at beat " + juce::String(startBeat) + ", which is not a finite beat between " +
                    beatRangeText() + ".");

    const juce::var midBase64Var = op.getProperty("midBase64");
    if (!midBase64Var.isString())
        return fail(where + "needs a string \"midBase64\" carrying a base64-encoded Standard MIDI File.");
    const juce::String midBase64 = midBase64Var.toString();
    if (midBase64.isEmpty())
        return fail(where + "has an empty \"midBase64\" - there is nothing to import.");

    // Checked against the STILL-ENCODED string before a single byte is decoded, so an oversized
    // blob is rejected as cheaply as any other length check - never by allocating a buffer for it
    // first and discovering it afterwards.
    if (midBase64.length() > TimelineOps::kMaxMidBlobBytes)
        return fail(where + "carries a \"midBase64\" of " + juce::String(midBase64.length()) +
                    " characters, exceeding the limit of " + juce::String(TimelineOps::kMaxMidBlobBytes) +
                    " - a .mid blob this large is not a note surface any more.");

    juce::MemoryOutputStream decoded;
    if (!juce::Base64::convertFromBase64(decoded, midBase64))
        return fail(where + "has a \"midBase64\" that is not valid base64.");

    juce::MemoryInputStream midiStream(decoded.getData(), decoded.getDataSize(), false);
    const auto imported = MidiClipFile::importFromStream(midiStream);
    if (!imported.ok)
        return fail(where + "carries a .mid blob that could not be imported: " + imported.message + ".");
    if (imported.tracks.empty())
        return fail(where + "carries a .mid blob with no notes in it - there is nothing to place.");

    const int existingClips = static_cast<int>(track->clips.size());
    if (existingClips + static_cast<int>(imported.tracks.size()) > TimelineDoc::kMaxClipsPerTrack)
        return fail(where + "would put " + juce::String(existingClips + static_cast<int>(imported.tracks.size())) +
                    " clips on track \"" + trackName + "\", exceeding the limit of " +
                    juce::String(TimelineDoc::kMaxClipsPerTrack) + " per track.");

    // A .mid blob's own beat positions are untrusted too: bound each track's derived clip end
    // (startBeat + ceil(last note end)) exactly as MidiClipFile::importIntoTrack computes it, so a
    // blob with huge tick values can't place a clip or note beyond kMaxPpqUntrusted merely because
    // startBeat itself was in range. Bounding the clip end bounds every note's end within it too,
    // since lengthBeats is always positive.
    int noteCount = 0;
    for (const auto& importedTrack : imported.tracks) {
        double lastEnd = 0.0;
        for (const auto& note : importedTrack.notes)
            lastEnd = std::max(lastEnd, note.startBeat + note.lengthBeats);
        const double clipLength = std::max(std::ceil(lastEnd), 1.0);
        if (!isLengthInBounds(clipLength) || !isBeatInBounds(startBeat + clipLength))
            return fail(where + "carries a .mid blob whose notes end at beat " + juce::String(startBeat + clipLength) +
                        ", which is beyond " + beatRangeText() + " once \"startBeat\" is added.");
        noteCount += static_cast<int>(importedTrack.notes.size());
    }
    totalNotes += noteCount;
    if (totalNotes > kMaxTotalNotesUntrusted)
        return fail(where + "takes the batch past " + juce::String(kMaxTotalNotesUntrusted) +
                    " notes in total, which is the most one set of timeline operations may author.");

    // MidiClipFile::importIntoTrack is the same one-mutation-per-clip/note shape every other op
    // here uses (no batching, no undo of its own) - reused rather than restated, and everything
    // above already proved it has room to succeed.
    if (!MidiClipFile::importIntoTrack(doc, trackId, startBeat, imported))
        return fail(where + "could not place the MIDI clip(s) on track \"" + trackName + "\".");

    parts.add("places " + countText(static_cast<int>(imported.tracks.size()), "MIDI clip", "MIDI clips") + " (" +
              countText(noteCount, "note", "notes") + ") from a .mid blob at beat " + beatText(startBeat) + " on \"" +
              trackName + "\"");
    return {};
}

// -- writeLane --------------------------------------------------------------------------------

TimelineOpsResult runWriteLane(const juce::String& where, juce::DynamicObject& op, TimelineDoc& doc,
                               const juce::AudioProcessorGraph& graph, juce::StringArray& parts) {
    if (const auto bad = unknownKey(op, {"op", "nodeUuid", "paramId", "points"}); bad.isNotEmpty())
        return fail(where + "has an unknown field \"" + bad +
                    "\". A writeLane op accepts only \"nodeUuid\", \"paramId\" and \"points\" - a lane's record "
                    "mode is never set here.");

    const juce::var nodeUuidVar = op.getProperty("nodeUuid");
    const juce::var paramIdVar = op.getProperty("paramId");
    if (!nodeUuidVar.isString() || !paramIdVar.isString())
        return fail(where + "needs a string \"nodeUuid\" and a string \"paramId\".");
    const juce::String nodeUuid = nodeUuidVar.toString();
    const juce::String paramId = paramIdVar.toString();
    if (nodeUuid.isEmpty() || paramId.isEmpty())
        return fail(where + "has an empty \"nodeUuid\" or \"paramId\". A lane is identified by the parameter it "
                            "automates, so neither may be blank.");

    // The binding must resolve against the LIVE graph, exactly as validateTimeline requires: an
    // orphaned binding is a state the app RECOVERS from when a module disappears under an existing
    // lane, not one untrusted input gets to author from nothing.
    auto* processor = findProcessorByUuid(graph, nodeUuid);
    if (processor == nullptr)
        return fail(where + "writes to node uuid \"" + nodeUuid +
                    "\", which no module in the current patch has. Automate a module that exists.");

    const AutomationLane* existing = doc.getLaneForParam(nodeUuid, paramId);
    // Resolved through the SAME shared resolver the audio path and TimelineReconciler use —
    // an exact id match, or (for a hosted plugin with no stable ids at all) the existing lane's
    // stored paramIndexHint. A brand-new lane has no hint yet, so this is exact-match-only until it
    // exists.
    const auto resolved = resolveLaneParameter(processor, paramId, existing != nullptr ? existing->paramIndexHint : -1);
    if (!resolved.resolved())
        return fail(where + "writes parameter \"" + paramId + "\", which module \"" + processor->getName() +
                    "\" does not have. Use one of that module's parameter ids.");

    const juce::var pointsVar = op.getProperty("points");
    if (!pointsVar.isArray())
        return fail(where + "needs a \"points\" array.");
    const auto& pointList = *pointsVar.getArray();
    if (pointList.isEmpty())
        return fail(where + "has an empty \"points\" array - there is nothing to write.");
    if (pointList.size() > TimelineDoc::kMaxBreakpointsPerLane)
        return fail(where + "writes " + juce::String(pointList.size()) + " points, exceeding the limit of " +
                    juce::String(TimelineDoc::kMaxBreakpointsPerLane) + " per lane.");

    // The value bounds. The live parameter's range is the authority (never a range a sender
    // supplied), and where an EXISTING lane's snapshot is narrower it narrows them further: not for
    // security, but because editBreakpoints would CLAMP a value into that snapshot, and a value we
    // would have to correct is a value the sender did not mean. A hosted-plugin parameter has
    // no NormalisableRange, so its bounds are exactly [0, 1] (laneValueBoundsFor — see
    // AutomationBinding.h) rather than something read off the parameter.
    const auto liveBounds = laneValueBoundsFor(resolved);
    double minAllowed = liveBounds.minValue;
    double maxAllowed = liveBounds.maxValue;
    if (existing != nullptr) {
        minAllowed = std::max(minAllowed, static_cast<double>(existing->range.minValue));
        maxAllowed = std::min(maxAllowed, static_cast<double>(existing->range.maxValue));
    }

    std::vector<AutomationLane::Breakpoint> points;
    points.reserve(static_cast<size_t>(pointList.size()));
    std::set<double> seenBeats;
    double minBeat = std::numeric_limits<double>::max();
    double maxBeat = std::numeric_limits<double>::lowest();

    for (const auto& pointVar : pointList) {
        auto* pointObj = pointVar.getDynamicObject();
        if (pointObj == nullptr)
            return fail(where + "has a point that is not an object.");
        if (const auto bad = unknownKey(*pointObj, {"beat", "value", "tension", "curve"}); bad.isNotEmpty())
            return fail(where + "has a point with an unknown field \"" + bad +
                        "\". A point accepts only \"beat\", \"value\", \"tension\" and \"curve\".");

        AutomationLane::Breakpoint point;
        if (!readOptionalDouble(pointObj->getProperty("beat"), point.beat) ||
            !readOptionalDouble(pointObj->getProperty("value"), point.value))
            return fail(where + "has a point with a non-numeric \"beat\" or \"value\".");
        if (!isBeatInBounds(point.beat))
            return fail(where + "has a point at beat " + juce::String(point.beat) +
                        ", which is not a finite beat between " + beatRangeText() + ".");
        if (!std::isfinite(point.value))
            return fail(where + "has a point at beat " + beatText(point.beat) + " with a non-finite value.");
        if (point.value < minAllowed || point.value > maxAllowed)
            return fail(where + "has a point at beat " + beatText(point.beat) + " with value " +
                        juce::String(point.value) + ", outside the range " + juce::String(minAllowed) + " to " +
                        juce::String(maxAllowed) + " of parameter \"" + paramId +
                        "\". Automation values are in the parameter's own units.");

        // Two points on one beat means two different values, one of which editBreakpoints would
        // silently drop ("last one wins"). Rejected, like every other case where a trusted path
        // would repair what a sender wrote.
        if (!seenBeats.insert(point.beat).second)
            return fail(where + "writes two points at beat " + beatText(point.beat) +
                        ". A lane holds at most one point per beat.");

        if (!readOptionalFloat(pointObj->getProperty("tension"), point.tension))
            return fail(where + "has a point at beat " + beatText(point.beat) + " with a non-numeric \"tension\".");
        if (point.tension < -1.0f || point.tension > 1.0f)
            return fail(where + "has a point at beat " + beatText(point.beat) + " with tension " +
                        juce::String(point.tension) + ", outside -1 to 1.");
        if (!readOptionalInt(pointObj->getProperty("curve"), point.curve))
            return fail(where + "has a point at beat " + beatText(point.beat) + " with a non-integer \"curve\".");
        if (point.curve < static_cast<int>(BreakpointCurve::Hold) ||
            point.curve > static_cast<int>(BreakpointCurve::Bezier))
            return fail(where + "has a point at beat " + beatText(point.beat) + " using curve " +
                        juce::String(point.curve) + ". Use 0 (hold), 1 (linear) or 2 (bezier).");

        minBeat = std::min(minBeat, point.beat);
        maxBeat = std::max(maxBeat, point.beat);
        points.push_back(point);
    }

    // find-or-create, the rule MainComponent::automateParameter implements for the user's own
    // "Automate this parameter" gesture: the lane if one already exists for this parameter
    // (anywhere in the doc — lane identity is doc-wide), otherwise a new lane on the document's ONE
    // Automation track, creating that track too when there is none yet.
    LaneId laneId;
    if (existing != nullptr) {
        laneId = existing->id;
    } else {
        TrackId automationTrack;
        for (const auto& track : doc.getTracks()) {
            if (track.kind == TrackKind::Automation) {
                automationTrack = track.id;
                break;
            }
        }
        if (!automationTrack.isValid()) {
            if (static_cast<int>(doc.getTracks().size()) >= TimelineDoc::kMaxTracks)
                return fail(where + "needs an Automation track, but the timeline is already at its limit of " +
                            juce::String(TimelineDoc::kMaxTracks) + " tracks.");
            automationTrack = doc.addTrack(TrackKind::Automation, "Automation");
            if (!automationTrack.isValid())
                return fail(where + "could not create the Automation track.");
        }

        if (const auto* track = doc.getTrack(automationTrack);
            track != nullptr && static_cast<int>(track->lanes.size()) >= TimelineDoc::kMaxLanesPerTrack)
            return fail(where + "would exceed the limit of " + juce::String(TimelineDoc::kMaxLanesPerTrack) +
                        " automation lanes on one track.");

        AutomationLane::RangeSnapshot range;
        range.minValue = static_cast<float>(liveBounds.minValue);
        range.maxValue = static_cast<float>(liveBounds.maxValue);
        range.defaultValue = static_cast<float>(laneDefaultValueFor(resolved));
        // Captured once, at creation, from the resolver's own exact-match result — never
        // re-derived afterwards. -1 for a non-plugin target.
        laneId = doc.addLane(automationTrack, nodeUuid, paramId, range, captureParamIndexHint(processor, paramId));
        if (!laneId.isValid())
            return fail(where + "could not create the automation lane for \"" + paramId + "\".");
    }

    // REPLACE the written span: every existing point from the payload's first beat to its last,
    // inclusive, goes; the payload's points take their place. One editBreakpoints call, so however
    // many points move it is ONE revision bump and one snapshot republish (the batched-commit
    // contract editBreakpoints provides), not one per point.
    std::vector<double> removeBeats;
    if (const auto* lane = doc.getLane(laneId)) {
        for (const auto& point : lane->points)
            if (point.beat >= minBeat && point.beat <= maxBeat)
                removeBeats.push_back(point.beat);

        const int kept = static_cast<int>(lane->points.size()) - static_cast<int>(removeBeats.size());
        if (kept + static_cast<int>(points.size()) > TimelineDoc::kMaxBreakpointsPerLane)
            return fail(where + "would leave the lane with " + juce::String(kept + static_cast<int>(points.size())) +
                        " points, exceeding the limit of " + juce::String(TimelineDoc::kMaxBreakpointsPerLane) +
                        " per lane.");
    }

    if (!doc.editBreakpoints(laneId, removeBeats, points))
        return fail(where + "could not write the points to the \"" + paramId + "\" lane.");

    const juce::String target = juce::String(processor->getName()) + " " + paramId;
    parts.add("writes " + countText(static_cast<int>(points.size()), "point", "points") + " to " + target +
              (minBeat == maxBeat ? " at beat " + beatText(minBeat)
                                  : " over beats " + beatText(minBeat) + "-" + beatText(maxBeat)));
    return {};
}

// -- the batch --------------------------------------------------------------------------------

/**
 * The ONE implementation of the batch, shared by validate() and apply(): validate() runs it
 * against a throwaway COPY of the document and keeps only the summary, apply() runs it against the
 * real one. Sharing the code is what makes "a preview can never describe an apply that then fails"
 * true by construction, instead of by two lists of rules being kept in step by hand.
 */
TimelineOpsResult runBatch(const juce::var& envelope, TimelineDoc& doc, const juce::AudioProcessorGraph& graph) {
    auto* rootObj = envelope.getDynamicObject();
    if (rootObj == nullptr)
        return fail("Timeline operations must be a JSON object carrying a \"timelineOps\" array.");

    const juce::var opsVar = rootObj->getProperty("timelineOps");
    if (!opsVar.isArray())
        return fail("\"timelineOps\" must be an array of operations.");
    const auto& ops = *opsVar.getArray();
    if (ops.isEmpty())
        return fail("\"timelineOps\" is empty - there is nothing to apply.");
    if (ops.size() > TimelineOps::kMaxOps)
        return fail("There are " + juce::String(ops.size()) + " timeline operations, exceeding the limit of " +
                    juce::String(TimelineOps::kMaxOps) + " in one batch.");

    juce::StringArray parts;
    std::int64_t totalNotes = 0;

    for (int i = 0; i < ops.size(); ++i) {
        auto* opObj = ops.getReference(i).getDynamicObject();
        const juce::String index = "timelineOps[" + juce::String(i) + "]";
        if (opObj == nullptr)
            return fail(index + " is not an object.");

        const juce::var nameVar = opObj->getProperty("op");
        if (!nameVar.isString())
            return fail(index + " has no string \"op\" naming the operation.");
        const juce::String opName = nameVar.toString();
        const juce::String where = index + " (" + opName + "): ";

        TimelineOpsResult result;
        if (opName == "addTrack")
            result = runAddTrack(where, *opObj, doc, parts);
        else if (opName == "placeClips")
            result = runPlaceClips(where, *opObj, doc, parts, totalNotes);
        else if (opName == "writeLane")
            result = runWriteLane(where, *opObj, doc, graph, parts);
        else if (opName == "placeMidiClip")
            result = runPlaceMidiClip(where, *opObj, doc, parts, totalNotes);
        else
            result = fail(index + " asks for unknown operation \"" + opName +
                          "\". The operations are \"addTrack\", \"placeClips\", \"writeLane\" and \"placeMidiClip\".");

        if (!result.ok)
            return result;
    }

    // Sentence-cased once at the end rather than per part, so the parts read as one sentence
    // ("Adds midi track \"Bass\"; places 1 clip …") however they are combined.
    juce::String summary = parts.joinIntoString("; ");
    if (summary.isNotEmpty())
        summary = summary.substring(0, 1).toUpperCase() + summary.substring(1);

    return {true, "Validated " + countText(ops.size(), "timeline operation", "timeline operations") + ".", summary};
}

} // namespace

bool TimelineOps::carriesOps(const juce::var& payload) {
    auto* rootObj = payload.getDynamicObject();
    return rootObj != nullptr && rootObj->hasProperty("timelineOps");
}

TimelineOpsResult TimelineOps::validate(const juce::var& envelope, const TimelineDoc& doc,
                                        const juce::AudioProcessorGraph& graph) {
    // The document is copied through its own serialisation and the batch is run against the COPY,
    // which is then thrown away: nothing here touches the live doc, and yet every op sees the
    // effect of the ones before it (a track added by op 0 is targetable by op 1), which a
    // check-only pass over the ops could only reproduce by modelling the document a second time.
    // Replaying our own toVar() output is the trusted path by definition — same reasoning as the
    // graph replay in AIIntegrationService::applyPatch's structural gate.
    TimelineDoc scratch;
    if (!scratch.fromVar(doc.toVar()))
        return fail("The timeline could not be copied for validation, so nothing was checked or applied.");

    return runBatch(envelope, scratch, graph);
}

TimelineOpsResult TimelineOps::apply(const juce::var& envelope, TimelineDoc& doc,
                                     const juce::AudioProcessorGraph& graph, AppUndoManager& undo) {
    // Validate first, on a copy. A rejection here means the live doc was never touched at all —
    // not "was touched and put back" — which is what "all-or-nothing" has to mean for the user.
    const auto preview = validate(envelope, doc, graph);
    if (!preview.ok)
        return preview;

    TimelineOpsResult applied;
    const juce::var before = doc.toVar();

    // ONE undo step for the whole batch, however many tracks, clips, notes and breakpoints it
    // touches — recordTimelineChange snapshots around the entire lambda (the same contract
    // MidiRecorder::stopAndCommit relies on for a take's clip plus its every note).
    const bool pushed = undo.recordTimelineChange(doc, [&] {
        applied = runBatch(envelope, doc, graph);

        // Unreachable: validate() just proved this exact batch against an identical document. Kept
        // because the cost of being wrong is a half-applied arrangement, and restoring the
        // pre-batch serialisation makes recordTimelineChange see no change and push nothing.
        if (!applied.ok)
            doc.fromVar(before);
    });

    if (!applied.ok)
        return applied;

    if (!pushed)
        return {true, "The timeline already matched these operations, so nothing changed.", preview.previewText};

    int opCount = 0;
    if (auto* rootObj = envelope.getDynamicObject())
        if (auto* opsArray = rootObj->getProperty("timelineOps").getArray())
            opCount = opsArray->size();

    return {true, "Applied " + countText(opCount, "timeline operation", "timeline operations") + " as one undo step.",
            preview.previewText};
}

} // namespace synth
