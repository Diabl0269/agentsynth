#pragma once

#include "Timeline/TimelineDoc/TimelineDoc.h"
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
 * @brief The app-side timeline tools: discrete, validated, previewable operations a model
 *        may ask for (addTrack, placeClips, writeLane, placeMidiClip), applied as ONE undo step.
 *
 * A **sibling** of a patch suggestion, never nested inside one — `"timelineOps"` is a distinct
 * envelope key from `"timeline"`, so a response may legitimately carry both side by side. Trust
 * posture is identical to a patch card: validate (untrusted) -> preview -> the user explicitly
 * clicks Apply -> apply. Nothing here is ever applied because a model asked for it.
 *
 * See TimelineOps.cpp's protocol notes (top of the file, inside `namespace synth`) for the full
 * writeup: the two-door trust boundary shared with patch suggestions, why the grammar deliberately
 * omits certain fields, and why the per-op checks reuse `TimelineValidator`'s rather than restate
 * them. Any invalid op rejects the whole envelope untouched — see `apply()`'s `@return` below.
 */
struct TimelineOps {
    /** Ops in one envelope. Bounds the batch itself, the way `kMaxTotalNotesUntrusted` bounds the
     *  whole payload rather than any one container. A single tool call has no business asking for
     *  more edits than this in one go. */
    static constexpr int kMaxOps = 64;

    /** Longest track/clip name an op may supply. Keeps `previewText` — which is rendered into a
     *  chat card — bounded by the grammar rather than by the sender's restraint. */
    static constexpr int kMaxNameChars = 128;

    /** Largest `"midBase64"` STRING a `placeMidiClip` op may supply, checked against the
     *  still-encoded string BEFORE decoding — cheap enough (a length check) to reject an oversized
     *  blob without ever allocating a buffer for it. 256 KiB of base64 is already a generously large
     *  MIDI file; a bigger one is not a note surface any more. */
    static constexpr int kMaxMidBlobBytes = 262144;

    /**
     * @brief True if `payload` carries a `"timelineOps"` key at all.
     *
     * See TimelineOps.cpp for why this is keyed on presence rather than well-formedness.
     */
    static bool carriesOps(const juce::var& payload);

    /**
     * @brief Validates the envelope WITHOUT applying it — the preview step.
     *
     * Mutates nothing: not `doc`, not `graph`, not `envelope`. On success `previewText` summarises
     * what an apply would do ("Adds midi track \"Bass\"; places 1 clip (8 notes) at 0-4 on
     * \"Bass\"; writes 12 points to Filter cutoff over beats 0-16").
     *
     * @param doc   the live document; read for cap headroom and for resolving `placeClips` targets.
     * @param graph the LIVE graph — a `writeLane` op's `(nodeUuid, paramId)` must resolve against
     *              it, and the resolved parameter's real range is what bounds the values.
     */
    static TimelineOpsResult validate(const juce::var& envelope, const TimelineDoc& doc,
                                      const juce::AudioProcessorGraph& graph);

    /**
     * @brief Validates again, then applies the WHOLE batch as ONE undo step.
     *
     * Wrapped in a single `AppUndoManager::recordTimelineChange`, so however many tracks, clips,
     * notes and breakpoints the batch touches, one Cmd+Z reverts all of it (the same contract
     * `MidiRecorder::stopAndCommit` gets for a take's clip plus its every note).
     *
     * See TimelineOps.cpp for per-op behaviour (addTrack/placeClips/writeLane/placeMidiClip),
     * documented beside each op's own implementation there.
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
