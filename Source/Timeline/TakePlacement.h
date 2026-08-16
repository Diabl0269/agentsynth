#pragma once

#include <cstdint>

namespace synth {

/**
 * @brief Where a finished audio take lands on the timeline.
 *
 * The whole of the record-commit's arithmetic, as a pure function, for two reasons: it is the one
 * piece of the recording pipeline that has to be EXACT (a sample of error is an audible flam), and
 * it is unreachable from a headless test as long as it lives inside MainComponent::commit.
 *
 * -- The two corrections --
 * 1. WHERE THE TAKE ACTUALLY STARTED. `RecordTapModule` reports the transport sample its frame 0
 *    was captured at (`TakeResult::captureStartTimelineSample`, read on the audio thread from the
 *    block's BlockTimeInfo) — without this anchor a take could be missing up to ~100 ms of head
 *    while still claiming to start at the punch.
 * 2. ROUND-TRIP LATENCY. A musician plays against what they HEAR. Grid position G leaves the
 *    speakers `outputLatency` samples after the graph rendered it; the note they play in response
 *    arrives back at our callback `inputLatency` (+ the graph's own reported latency) later still.
 *    So audio captured at timeline sample T was PLAYED at T - (input + graph + output) — that sum is
 *    `AudioEngine::getRecordingLatencySamples()`, and shifting the take back by it is what makes a
 *    take land where it was played rather than a round trip late.
 *
 * -- What comes out --
 * Take frame 0 sits at PLAYED timeline sample `S0 = captureStart - recordingLatency`. The clip must
 * not start before the punch (that is what a count-in's pre-roll is: heard, played over, but not
 * part of the take), so:
 *
 *     clipStartSample = max(punchSample, S0, 0)
 *     trimFrames      = clipStartSample - S0        // frames of the take BEFORE the clip window
 *     lengthFrames    = takeLength - trimFrames
 *
 * and the trim is expressed as `sourceStartSeconds` rather than by rewriting the WAV: **the file
 * keeps the pre-roll audio, the clip window merely excludes it**. Nothing is re-encoded, nothing is
 * lost, and dragging the clip's left edge back later reveals exactly what was played.
 *
 * The clamp at 0 is what keeps a take recorded from beat 0 honest: the shift would put its frame 0
 * at a NEGATIVE timeline sample, so the clip stays at 0 and the remainder moves into
 * `sourceStartSeconds` — the content never shifts relative to itself, only the window onto it does.
 */
struct TakePlacementInput {
    /** Frames the take actually wrote — `TakeResult::lengthSamples`. */
    std::int64_t takeLengthSamples = 0;

    /** `TakeResult::captureStartValid`. False (no block captured, or no transport playhead behind
     *  the tap) falls back to the simple placement: the clip starts at the punch, untrimmed. */
    bool captureStartValid = false;
    /** `TakeResult::captureStartTimelineSample`. */
    std::int64_t captureStartTimelineSample = 0;
    /** `TakeResult::captureStartBlockOffset` — frames of the anchor block that were not captured. */
    int captureStartBlockOffset = 0;

    /** The take's punch-in beat: where the clip is allowed to start at the earliest. */
    double punchInBeat = 0.0;

    /** `AudioEngine::getRecordingLatencySamples()` — input + graph + output. */
    int recordingLatencySamples = 0;

    double sampleRate = 44100.0;
    double bpm = 120.0;

    /** Floor for the committed clip's length, in beats (the caller's kMinAudioClipLengthBeats). */
    double minClipLengthBeats = 1.0 / 32.0;
};

struct TakePlacement {
    /** False means "commit nothing": an empty take, or one that ended before its own punch (all of
     *  it is pre-roll). The caller must not create a clip. */
    bool hasContent = false;

    double clipStartBeat = 0.0;
    double clipLengthBeats = 0.0;
    /** What the clip's `sourceStartSeconds` must be — the head of the FILE the window skips. */
    double sourceStartSeconds = 0.0;

    /** The same two answers in samples, exactly as computed before the beat conversion. Reported so
     *  tests (and any future sample-domain caller) never have to convert back through a double. */
    std::int64_t clipStartSample = 0;
    std::int64_t trimFrames = 0;
    std::int64_t lengthFrames = 0;
};

/** Pure; no JUCE, no clock, no I/O. See the header comment for the full argument. */
TakePlacement computeTakePlacement(const TakePlacementInput& input) noexcept;

} // namespace synth
