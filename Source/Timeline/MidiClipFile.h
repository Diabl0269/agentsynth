#pragma once

#include "TimelineDoc.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <vector>

namespace synth {

/**
 * @brief Standard MIDI File (SMF) import/export for timeline clips.
 *
 * The escape hatch while the piano roll stays minimal, the standard interchange format for the
 * arrangement pillar, and the intended future AI patching surface for note data: a .mid blob
 * carries no file paths and no plugin identifiers, unlike almost anything else a model could hand
 * back. That is why every design choice below leans bounds-checking-strict rather than permissive.
 *
 * TimelineDoc has no tempo map (tempo lives on TransportService, not the document), so tempo,
 * time-signature and every other meta event in an imported file are read but IGNORED for anything
 * except a track's name. Exported files carry no tempo event either. SMPTE time format is rejected
 * outright — beats-per-quarter-note (PPQ) is the only time format this class understands.
 *
 * Note pairing: FIFO per (pitch, channel) — the earliest still-open note-on for that key is closed
 * by the next note-off for the same key, so overlapping same-pitch retriggers pair up in the order
 * they were struck (same convention as MidiRecorder). A note-on with velocity 0 is treated as a
 * note-off. A dangling note-on, or a paired note that resolves to a non-positive length, is floored
 * to kMinNoteLengthBeats rather than dropped or rejected.
 *
 * Bounds: a track whose note count would exceed TimelineDoc::kMaxNotesPerClip rejects the WHOLE
 * import (ok=false) rather than truncating — this is a future untrusted surface (AI-authored .mid
 * blobs feed through this code), so silently truncating would let a hostile or buggy file degrade
 * rather than fail loudly. Every field pairTrack produces (pitch 0..127, velocity 1..127, channel
 * 1..16) is structurally guaranteed valid by juce::MidiMessage's own encoding.
 */
class MidiClipFile {
public:
    // Ticks-per-quarter-note used when EXPORTING a clip. A "beat" in TimelineDoc IS a quarter
    // note, so this doubles as the export ticks-per-beat divisor. Chosen so the tick rounding
    // writeTo() applies (round to nearest integer tick) bounds round-trip error to 1/(2*960) =
    // 1/1920 beat.
    static constexpr int kExportPpq = 960;

    // Floor applied to a note whose computed length is non-positive: a dangling note-on with no
    // matching note-off anywhere later in its track, or an on/off pair landing on the exact same
    // tick. Same value and rationale as MidiRecorder::kMinNoteLengthBeats.
    static constexpr double kMinNoteLengthBeats = 1.0 / 32.0;

    // One track's worth of notes read from an SMF. Reuses TimelineDoc::MidiNote as-is — `id` is
    // left default (NoteId{}, the invalid sentinel) since these notes have not been assigned into
    // a doc yet; every other field is exactly what TimelineDoc::addNote expects. `startBeat` is
    // measured from the FILE's own beat zero (ticks / the file's own PPQ), which is already
    // clip-relative for a clip planted at that same beat zero — importIntoTrack relies on this to
    // place notes without re-basing them.
    struct ImportedTrack {
        juce::String name;           // from a MetaEvent track-name event, if present; empty otherwise
        std::vector<MidiNote> notes; // sorted by (startBeat, pitch), same invariant as Clip::notes
    };

    struct ImportResult {
        bool ok = false;
        juce::String message;              // empty on success; a human-readable reason on failure
        std::vector<ImportedTrack> tracks; // empty tracks are never included
    };

    // Parses a Standard MIDI File (type 0 or 1) from a stream — NOT only from a File, so a future
    // caller can feed an in-memory .mid blob straight from a model response. See the class
    // comment for the pairing/tempo/bounds contract. Rejects (ok=false):
    //  - a stream that isn't a valid SMF (bad header, truncated) — message is generic, no crash;
    //  - a file using SMPTE time format — message mentions "SMPTE";
    //  - any track whose note count would exceed TimelineDoc::kMaxNotesPerClip — rejects the WHOLE
    //    import, not just that track.
    static ImportResult importFromStream(juce::InputStream& stream);
    static ImportResult importFromFile(const juce::File& file);

    // Convenience: creates one clip per non-empty ImportedTrack on `trackId`, all starting at the
    // same `startBeat` (stacked, not sequenced — the caller decides layout). Each clip's length is
    // ceil(last note end) within that track, floored at 1 beat. All-or-nothing against the doc's
    // kMaxClipsPerTrack cap: if the track doesn't have room for every incoming clip, NOTHING is
    // added and this returns false (the doc is left byte-identical). Also returns false, with no
    // mutation, if `result.ok` is false, `trackId` doesn't resolve to a track, or there is nothing
    // non-empty to import.
    //
    // One doc mutation per clip (addClip) plus one per note (addNote) — this function performs no
    // batching of its own. The caller is responsible for wrapping the whole call in
    // AppUndoManager::recordTimelineChange for undo; this function never touches AppUndoManager.
    static bool importIntoTrack(TimelineDoc& doc, TrackId trackId, double startBeat, const ImportResult& result);

    // Exports one clip as an SMF type 1 file: a single tempo-less track holding every note in the
    // clip as a noteOn/noteOff pair at kExportPpq ticks-per-quarter-note (clip-relative beats,
    // exactly as stored — no truncation to the clip's own lengthBeats, matching the model's own
    // "notes may overhang a clip" policy), an explicit end-of-track event at max(the clip's own
    // length, the last event), PPQ time format only. Returns false if `clipId` doesn't resolve to a
    // clip in `doc`, or if the stream write fails.
    static bool exportClip(const TimelineDoc& doc, ClipId clipId, juce::OutputStream& stream);
    static bool exportClipToFile(const TimelineDoc& doc, ClipId clipId, const juce::File& file);

private:
    MidiClipFile() = delete;
};

} // namespace synth
