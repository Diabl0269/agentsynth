#pragma once

#include "BlockTimeInfo.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

/**
 * @brief The metronome click generator — audio-thread synth, summed POST-graph.
 *
 * `renderClicks()` runs from `AudioEngine::renderPass` AFTER the graph's processBlock and BEFORE
 * master-mute's zero-fill, so the click is never seen by anything inside the graph — no module can
 * tap it, no in-graph recording tap can capture it. `BounceExporter::bounce()` captures exactly that
 * post-graph buffer though, so it forces this class off for the render's duration instead of relying
 * on anything structural (see `BounceExporter.cpp`). Master mute still clears the whole buffer after
 * this call, killing the click along with everything else — deliberate (see `AudioEngine::renderPass`).
 *
 * Beat-crossing scan mirrors `TimelineMidiSourceModule`'s loop-wrap-aware scan: a block that wraps
 * the loop is split into two ranges so no beat is double-counted or missed across the wrap. A beat
 * is a downbeat when it lands on a bar start (`beatsPerBar = timeSigNumerator * 4 / timeSigDenominator`);
 * signatures where that never divides evenly (e.g. 5/8) just get plain, unaccented clicks.
 *
 * Clicks are short decaying sine bursts spanning block boundaries: `kNumVoices` (4) persistent
 * voices continue every call rather than resetting per block. Voice stealing takes the one with the
 * fewest samples remaining. Zero allocation, zero locks, zero logging (see CLAUDE.md's
 * no-high-frequency-logging rule).
 *
 * `setEnabled`/`isEnabled` is the user-facing toggle (message thread writes, audio thread reads).
 * `setForcedOn`/`isForcedOn` is OR'd with it so a count-in pre-roll can force the click audible
 * regardless of the user toggle. With neither set, `renderClicks()` is a complete no-op (see
 * `DisabledMetronomeIsSilentAndFree` in `Tests/MetronomeTests.cpp`).
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

    // Count-in: forces the click audible through the pre-roll regardless of the user toggle.
    void setForcedOn(bool forced) noexcept { forcedOn_.store(forced, std::memory_order_relaxed); }
    bool isForcedOn() const noexcept { return forcedOn_.load(std::memory_order_relaxed); }

    // Called from AudioEngine::handleStreamFormatChange when the engine's sample rate
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
