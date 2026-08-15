#pragma once

#include "TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @brief Why a timeline JSON failed validation, so a caller (and the UI) can say what was wrong.
 *
 * Same contract as PatchValidationError: the enumerator is the machine-readable half and the
 * accompanying message the human-readable one, and a rejection reports the FIRST problem found —
 * fixing one class of error can unmask the next.
 */
enum class TimelineValidationError {
    None,
    MalformedRoot,
    TooManyTracks,
    TooManyClips,
    TooManyNotes,
    TooManyLanes,
    TooManyBreakpoints,
    BeatOutOfBounds,
    NoteOutOfRange,
    ValueOutOfParamRange,
    UnresolvableBinding,
    AssetNotAllowed,
    RecordModeNotAllowed,
    ReservedKindNotAllowed,
    InternalError,
};

/** @brief Result of validating timeline JSON: whether it passed, and if not, why. */
struct TimelineValidationResult {
    bool ok = true;
    TimelineValidationError error = TimelineValidationError::None;
    juce::String message;
};

/**
 * @brief Stable, human-readable name for a TimelineValidationError value.
 *
 * The strings match the enumerator names, so a log line or a rejection histogram can be read
 * straight against this header (see patchValidationErrorName for the same idiom).
 */
juce::String timelineValidationErrorName(TimelineValidationError error);

// -- Untrusted-only hard caps ---------------------------------------------------------------
// The per-container caps live on TimelineDoc (kMaxTracks, kMaxClipsPerTrack, kMaxNotesPerClip,
// kMaxLanesPerTrack, kMaxBreakpointsPerLane) and are referenced, never duplicated: one number,
// enforced by the mutation API, by fromVar and by this gate. The two below exist only here,
// because they bound the WHOLE untrusted payload rather than any one container.

/** Notes summed across every clip in the document. The per-clip cap (16384) multiplied out by
 *  the track/clip caps allows billions; a single tool call has no business authoring more than
 *  this, and the total is what actually bounds the work an apply does. */
inline constexpr int kMaxTotalNotesUntrusted = 65536;

/** The largest beat position (and the largest length in beats) untrusted input may name. At
 *  120 BPM this is ~14 hours of music — far past anything a model should be authoring, and far
 *  short of where double-precision beat arithmetic starts losing musically relevant resolution. */
inline constexpr double kMaxPpqUntrusted = 100000.0;

/**
 * @brief Untrusted gate for AI/tool-supplied timeline JSON (the TimelineDoc toVar dialect).
 *
 * ### The two-door model
 *
 * TL0-4 made `AIStateMapper::validatePatch(trusted=false)` REFUSE a `"timeline"` key in a patch
 * suggestion, and that refusal is permanent: AI timeline data will never ride the patch grammar,
 * because a patch is applied to the graph and a timeline is not part of a graph. This function is
 * the OTHER door — the deliberate commit that opens what TL0-4 closed, through its own guarded
 * entrance rather than the patch path. TL8-4's discrete app-side timeline tools (add-track,
 * place-clips, write-lane) validate their payloads here before any of them touches the doc.
 * Neither door weakens the other: the patch-grammar refusal stays exactly as it is.
 *
 * ### Contract
 *
 * Validates STRICTLY and never mutates anything — not the doc, not the graph, not `timelineVar`.
 * The caller applies via `TimelineDoc::fromVar` (all-or-nothing) only after this passes.
 *
 * On success, `TimelineDoc::fromVar(timelineVar)` is guaranteed to accept the same var: the last
 * thing this function does is prove it by loading the document into a throwaway doc. A var that
 * passes every named check but is still refused by the loader yields InternalError — the two have
 * drifted, and the caller must not apply it either way.
 *
 * Untrusted input is REJECTED where the trusted paths repair or clamp. `fromVar` clamps a
 * breakpoint's tension into [-1, 1] and its value into the lane's range snapshot, and repairs a
 * broken sort order; none of that happens for data a model wrote — a value we had to correct is a
 * value the model did not mean, so it is refused with a message saying so instead of silently
 * becoming a different value.
 *
 * ### What this refuses that a trusted load accepts
 *
 *  - Audio assets. Any clip carrying a non-empty `assetRef` is refused outright (AssetNotAllowed):
 *    assets, like plugin state blobs and a node's `"state"` object, stay trusted-only forever —
 *    honouring an untrusted one turns a suggestion into an arbitrary file read.
 *  - Recording. A lane may only ask for Read or Off (RecordModeNotAllowed) — untrusted input can
 *    never arm a lane to capture the user's gestures.
 *  - Orphans by authoring. Every non-empty binding (a track's `bindingUuid`, a lane's
 *    (nodeUuid, paramId) pair) must resolve against the LIVE graph. An orphaned binding is a state
 *    the app recovers from when a node disappears under it (TL2-6); it is not a state untrusted
 *    input gets to author from nothing.
 *  - Unknown top-level keys. See the note on PatchDocument in the .cpp: the trusted document path
 *    PRESERVES keys it does not understand so a newer file survives an older build; here they are
 *    refused, because nothing on the untrusted side needs forward-compatibility and an ignored key
 *    is exactly how a future build starts honouring something today's gate never inspected.
 *  - Reserved track kinds, and any value out of the caps above.
 *
 * @param timelineVar the candidate document, in the dialect TimelineDoc::toVar writes.
 * @param graph       the LIVE graph, used to resolve bindings. A lane's value bounds come from the
 *                    resolved parameter's real range — never from the lane's own RangeSnapshot,
 *                    which is data the model wrote and therefore cannot be the authority on what
 *                    the parameter accepts.
 */
TimelineValidationResult validateTimeline(const juce::var& timelineVar, const juce::AudioProcessorGraph& graph);

} // namespace synth
