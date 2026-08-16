#pragma once

#include "BlockTimeInfo.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

/**
 * @brief TL5-6: the metronome click generator — audio-thread synth, summed POST-graph.
 *
 * -- Why post-graph, and why that is the whole safety argument -------------------------------
 *
 * `renderClicks()` is called from `AudioEngine::renderPass`, AFTER `mainProcessorGraph.processBlock`
 * has already run and BEFORE `renderNextBlock`'s master-mute zero-fill. The click is therefore never
 * seen by anything INSIDE the graph — no module can tap it, no in-graph recording tap (TL6) can
 * capture it, and a `BounceExporter::bounce()` render (which captures exactly the graph's own output
 * buffer) would otherwise pick it up too, which is why the bounce path forces this class off for the
 * duration of a render (see `BounceExporter.cpp`) rather than relying on anything structural to keep
 * it out. Master mute clears the WHOLE buffer after this call runs, so it kills the click along with
 * everything else — deliberate, not an oversight (see `AudioEngine::renderPass`).
 *
 * -- Beat-crossing scan -----------------------------------------------------------------------
 *
 * Mirrors `TimelineMidiSourceModule`'s loop-wrap-aware scan: a block that does not wrap the loop is
 * one beat range `[startPpq, endPpq)`; a block that does is TWO — the primary range
 * `[startPpq, loopEndPpq)` at offsets `[0, loopWrapSample)`, then the wrapped range starting at
 * `loopStartPpq` for the remaining offsets. Every INTEGER beat inside a range starts one click:
 * `beat == floor-or-above(rangeStart)`, `beat < rangeEnd`, stepping by 1.0 — so a beat that lands
 * exactly on a range's start (transport start, or a wrap landing on an integer beat) still clicks,
 * and no beat is ever double-counted across two blocks that tile exactly.
 *
 * A beat is a downbeat (bar start) when `fmod(beat, beatsPerBar) == 0` (epsilon-tolerant), with
 * `beatsPerBar = timeSigNumerator * 4 / timeSigDenominator` — the same "a beat is always a quarter
 * note" convention `TimelineTransportBar`/`TransportService::getPosition()` use. NOTE: for a time
 * signature whose denominator does not divide `timeSigNumerator * 4` evenly (e.g. 5/8), no integer
 * beat ever lands exactly on a bar start, so every click in that signature is a plain (non-accented)
 * beat — a graceful degradation, not a crash, and not a signature this class was asked to accent.
 *
 * -- Click synthesis and the 4-voice pool -----------------------------------------------------
 *
 * A click is a short exponentially-decaying sine burst: 1600 Hz / amplitude 0.35 on a downbeat,
 * 1050 Hz / amplitude 0.25 otherwise, decaying to -60 dB over `kDecaySeconds` (~4 ms). Clicks SPAN
 * block boundaries — `kNumVoices` (4) persistent voices `{samplesRemaining, phase, phaseInc, amp,
 * decayMul}` are continued every call, not reset per block, so a click that starts 5 samples before
 * a block ends keeps ringing into the next call with no phase or amplitude discontinuity. On
 * overflow (a 5th click while all 4 are still ringing) the OLDEST voice — the one with the fewest
 * `samplesRemaining` left, since every click starts with the same countdown — is stolen; there is no
 * silent voice for the metronome to run out of at any tempo this decay length is audible at.
 *
 * `renderClicks()` first continues whatever was ringing from the previous call (from sample 0),
 * THEN scans for new crossings in this block — so a voice a new crossing steals still gets to finish
 * whatever fraction of its own decay this call has room for before being overwritten.
 *
 * Zero allocation, zero locks, zero logging (see the "No high-frequency logging" rule in CLAUDE.md)
 * — every voice lives in a fixed array, and the scan does only arithmetic and buffer writes.
 *
 * -- Enabled vs. forced-on ---------------------------------------------------------------------
 *
 * `setEnabled`/`isEnabled` is the user-facing toggle (message thread only writes it; the audio
 * thread only reads). `setForcedOn`/`isForcedOn` is OR'd with it: TL5-6's count-in pre-roll forces
 * the click audible for the pre-roll bars even when the user's toggle is off, so the performer can
 * hear the count regardless of whether they'd normally want a click during playback. When NEITHER is
 * set, `renderClicks()` is a complete no-op — it does not touch the buffer and does not advance any
 * voice state (see `DisabledMetronomeIsSilentAndFree` in `Tests/MetronomeTests.cpp`), so a disabled
 * metronome costs nothing beyond the two atomic loads that prove it is disabled.
 */
class Metronome {
public:
    Metronome() = default;

    // Audio thread only. Renders directly into `buffer` (additive — it does not clear anything),
    // scanning `info` for new beat crossings only while `info.playing`; already-ringing voices from
    // a previous call continue regardless of the playing flag, so a click started just before a
    // stop still finishes cleanly rather than being cut off mid-decay.
    void renderClicks(juce::AudioBuffer<float>& buffer, const BlockTimeInfo& info) noexcept;

    // Message thread writes; audio thread reads (relaxed — see the class comment).
    void setEnabled(bool enabled) noexcept { enabled_.store(enabled, std::memory_order_relaxed); }
    bool isEnabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // TL5-6 count-in: forces the click audible through the pre-roll regardless of the user toggle.
    void setForcedOn(bool forced) noexcept { forcedOn_.store(forced, std::memory_order_relaxed); }
    bool isForcedOn() const noexcept { return forcedOn_.load(std::memory_order_relaxed); }

    // TL6-9: called from AudioEngine::handleStreamFormatChange when the engine's sample rate
    // changes. A voice ringing across the boundary carries a `phaseInc`/`decayMul` computed for the
    // OLD rate (see startClick) — continuing to render it against the NEW rate's sample clock would
    // make it audibly the wrong pitch AND the wrong length. Silencing it outright is simpler (and no
    // more audible a discontinuity) than rescaling in place: the next beat crossing starts a fresh
    // voice at the new rate exactly as normal. Same "audio thread, no allocation" contract as
    // renderClicks() — called from the prepare path, which never overlaps a render pass.
    void resetVoices() noexcept {
        for (auto& voice : voices_)
            voice.samplesRemaining = 0;
    }

    // Diagnostics / tests only — not part of the audio contract. Counts voices still ringing.
    int getActiveVoiceCountForTest() const noexcept {
        int count = 0;
        for (const auto& voice : voices_)
            if (voice.samplesRemaining > 0)
                ++count;
        return count;
    }

private:
    struct Voice {
        int samplesRemaining = 0;
        double phase = 0.0;
        double phaseInc = 0.0;
        float amp = 0.0f;
        float decayMul = 1.0f;
    };

    static constexpr int kNumVoices = 4;
    static constexpr double kDecaySeconds = 0.004; // ~4 ms to -60 dB
    static constexpr double kDecayFloorDb = -60.0;
    static constexpr double kDownbeatFrequencyHz = 1600.0;
    static constexpr double kOtherBeatFrequencyHz = 1050.0;
    static constexpr float kDownbeatAmplitude = 0.35f;
    static constexpr float kOtherBeatAmplitude = 0.25f;

    // Renders one voice's remaining samples into `buffer`, starting at `fromSample`, for at most
    // `numSamplesInBlock - fromSample` samples (however many of its own `samplesRemaining` fit).
    // Additive: existing content in `buffer` is preserved, matching a metronome click summed on top
    // of whatever the graph already produced.
    static void renderVoiceInto(juce::AudioBuffer<float>& buffer, Voice& voice, int fromSample,
                                int numSamplesInBlock) noexcept;

    // Finds a free voice (samplesRemaining <= 0), or steals the one with the fewest samples left —
    // "oldest" in the sense that every voice starts with an identical countdown, so the smallest
    // remaining count is the one that has been ringing the longest.
    Voice& allocateVoice() noexcept;

    // Starts a new click at `offset` within THIS block and immediately renders whatever fraction of
    // it fits before `numSamplesInBlock`; the remainder (if any) carries over via the voice's own
    // persistent state for the next call.
    void startClick(juce::AudioBuffer<float>& buffer, int offset, int numSamplesInBlock, bool downbeat,
                    double sampleRate) noexcept;

    // Scans every integer beat in `[rangeStart, rangeEnd)`, mapping each to a block-relative sample
    // offset (`baseOffset` + the beat's distance from `rangeStart` in samples, clamped into
    // `[baseOffset, lastSample]`) and starting a click there.
    void scanRange(juce::AudioBuffer<float>& buffer, double rangeStart, double rangeEnd, int baseOffset,
                   double beatsPerSample, double beatsPerBar, int lastSample, int numSamplesInBlock,
                   double sampleRate) noexcept;

    std::atomic<bool> enabled_{false};
    std::atomic<bool> forcedOn_{false};
    Voice voices_[kNumVoices];

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(Metronome)
};

} // namespace synth
