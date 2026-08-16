#pragma once

#include "TimelineDoc.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

class AppUndoManager; // Source/AppUndoManager.h — global namespace, like every other user of it.

namespace synth {

/** @brief Outcome of validating or applying a timeline-ops envelope. */
struct TimelineOpsResult {
    /** False means NOTHING was applied — validation is all-or-nothing (see TimelineOps). */
    bool ok = true;
    /** On failure, the first problem found, naming the op that carries it ("timelineOps[2]
     *  (placeClips): …") so it can be handed straight back to a model as the correction to make.
     *  On success, a short statement of what happened. */
    juce::String message;
    /** The human-readable summary of what the batch DOES, for the chat card's preview — populated
     *  on success by both validate() and apply(), empty on failure. Deterministic: the same
     *  envelope against the same doc always produces the same string. */
    juce::String previewText;
};

/**
 * @brief The app-side timeline tools (TL8-4): discrete, validated, previewable operations a model
 *        may ask for, applied as ONE undo step.
 *
 * ### The envelope
 *
 * @code
 * { "timelineOps": [
 *     { "op": "addTrack",   "kind": "midi", "name": "Bass" },
 *     { "op": "placeClips", "track": "Bass",           // or { "index": 0 }
 *       "clips": [ { "startBeat": 0, "lengthBeats": 4, "name": "A",
 *                    "notes": [ { "startBeat": 0, "lengthBeats": 1,
 *                                 "pitch": 36, "velocity": 100, "channel": 1 } ] } ] },
 *     { "op": "writeLane",  "nodeUuid": "…", "paramId": "cutoff",
 *       "points": [ { "beat": 0, "value": 800, "tension": 0, "curve": 1 } ] }
 * ] }
 * @endcode
 *
 * This is the CLIENT half of the TL8-2 platform capabilities: the model emits this shape through
 * its own capability schema, and it is a **sibling** of a patch suggestion, never nested inside
 * one. `AIStateMapper::validatePatch(trusted=false)` still refuses a `"timeline"` key inside patch
 * JSON and always will (TL0-4, docs/AI_Engine.md §5c "the two-door model") — timeline data reaches
 * the app through this door or not at all. Because `"timelineOps"` is a different key from
 * `"timeline"`, a response may legitimately carry a patch and an ops envelope side by side; each is
 * validated and applied by its own gate, with its own Apply affordance.
 *
 * ### Trust posture — identical to a patch card
 *
 * validate (untrusted) -> preview to the user -> the user explicitly clicks Apply -> apply. Nothing
 * here is ever applied because a model asked for it; a person has to agree to it first, having read
 * `previewText`.
 *
 * ### The per-op checks are `validateTimeline`'s, reused rather than restated
 *
 * Same caps (`TimelineDoc::kMaxTracks`/`kMaxClipsPerTrack`/`kMaxNotesPerClip`/`kMaxLanesPerTrack`/
 * `kMaxBreakpointsPerLane`, `kMaxTotalNotesUntrusted`, `kMaxPpqUntrusted`), same bounds, and the
 * same rule that untrusted input is **REJECTED where a trusted path would clamp or repair**: pitch
 * 128 is refused, not rewritten to 127, and a breakpoint value outside the parameter's range is
 * refused rather than pulled inside it. Lane values are checked against the LIVE parameter's range,
 * never against a range snapshot the sender supplied.
 *
 * Deliberately absent from the grammar, which is how assets and record arming stay unreachable
 * (rather than being refused field-by-field): an op has no `assetRef`, no `recordMode`, no
 * `bindingUuid`, and no track kind beyond `midi`/`automation`. **Unknown fields inside an op are
 * REJECTED**, so a future field cannot be smuggled past a gate that never inspected it — the same
 * reasoning behind `validateTimeline`'s unknown-top-level-key refusal. Unknown keys at the
 * ENVELOPE root are ignored, because that is where the sibling patch's own keys live.
 *
 * ### All-or-nothing
 *
 * Any invalid op rejects the whole envelope and the doc is left untouched — validate() proves the
 * batch against a throwaway copy of the document before apply() runs a line of it against the real
 * one, and both go through the same code, so a preview cannot describe an apply that then fails.
 */
struct TimelineOps {
    /** Ops in one envelope. Bounds the batch itself, the way `kMaxTotalNotesUntrusted` bounds the
     *  whole payload rather than any one container. A single tool call has no business asking for
     *  more edits than this in one go. */
    static constexpr int kMaxOps = 64;

    /** Longest track/clip name an op may supply. Keeps `previewText` — which is rendered into a
     *  chat card — bounded by the grammar rather than by the sender's restraint. */
    static constexpr int kMaxNameChars = 128;

    /**
     * @brief True if `payload` carries a `"timelineOps"` key at all.
     *
     * Deliberately keyed on PRESENCE, not on well-formedness: a malformed `"timelineOps"` is
     * surfaced as a rejection the user can see rather than silently dropped, which is the same
     * reason applyPatch() never swallows a validation failure.
     */
    static bool carriesOps(const juce::var& payload);

    /**
     * @brief Validates the envelope WITHOUT applying it — the preview step.
     *
     * Mutates nothing: not `doc`, not `graph`, not `envelope`. On success `previewText` summarises
     * what an apply would do ("Adds midi track \"Bass\"; places 1 clip (8 notes) at 0-4 on
     * \"Bass\"; writes 12 points to Filter cutoff over beats 0-16").
     *
     * @param doc   the live document. Read for cap headroom and for resolving `placeClips`
     *              targets; ops later in the batch see the effect of earlier ones (a track added by
     *              op 0 is targetable by op 1).
     * @param graph the LIVE graph. A `writeLane` op's `(nodeUuid, paramId)` must resolve against
     *              it, and the resolved parameter's real range is what bounds the values.
     */
    static TimelineOpsResult validate(const juce::var& envelope, const TimelineDoc& doc,
                                      const juce::AudioProcessorGraph& graph);

    /**
     * @brief Validates again, then applies the WHOLE batch as ONE undo step.
     *
     * Wrapped in a single `AppUndoManager::recordTimelineChange`, so however many tracks, clips,
     * notes and breakpoints the batch touches, one Cmd+Z reverts all of it (the same contract
     * MidiRecorder::stopAndCommit gets for a take's clip plus its every note).
     *
     * Per-op behaviour worth knowing before calling:
     *  - **addTrack** creates the DOC track only — no graph node, no Track In wiring. Binding a
     *    track to a module is a user (or host) gesture, so the new track is unbound and says so in
     *    `previewText`. `kind` is `"midi"` or `"automation"`; `"audio"` is not offered, because an
     *    audio track without an asset-bearing clip is an empty row and assets are trusted-only.
     *  - **placeClips** targets a MIDI track by exact name or by `{"index": N}`. A name matching no
     *    track, or more than one, rejects the whole batch rather than guessing.
     *  - **writeLane** find-or-creates the lane for `(nodeUuid, paramId)` on the document's
     *    Automation track, creating that track too if the document has none (the TL5-9
     *    find-or-create rule, exactly as `MainComponent::automateParameter` does it), then REPLACES
     *    every existing point inside the written span — min..max beat of the payload, inclusive —
     *    in one `editBreakpoints` mutation.
     *
     * @return `ok == false` with the doc completely untouched if anything about the envelope is
     *         invalid. `ok == true` when the batch applied; `message` says whether an undo step was
     *         pushed (a batch that asks for exactly the state already in place changes nothing and
     *         correctly leaves no undo entry).
     */
    static TimelineOpsResult apply(const juce::var& envelope, TimelineDoc& doc, const juce::AudioProcessorGraph& graph,
                                   AppUndoManager& undo);
};

} // namespace synth
