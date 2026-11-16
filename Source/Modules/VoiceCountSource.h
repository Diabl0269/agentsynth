#pragma once

#include <algorithm>
#include <atomic>

// Read from the message thread: implementations return a value the audio thread published (an atomic), never walk voice
// arrays the audio thread writes.

namespace synth {

class VoiceCountSource {
public:
    virtual ~VoiceCountSource() = default;

    /** Voices sounding at the end of the last rendered block (held or ringing out). */
    virtual int displayVoiceCount() const noexcept = 0;
};

/** Holds the largest voice count seen during the last ~200 ms of audio, so a note shorter than the status bar's poll
    interval still shows. The audio thread calls `publish` once per block; the message thread reads `value()`. Inside
   the window the value is the maximum of the instantaneous counts (two short notes in a row read 1, never 2); once the
    window passes with nothing held it reads 0. Header-only, lock-free and free of JUCE types. */
class VoiceCountLatch {
public:
    static constexpr double kHoldSeconds = 0.2;

    /** Audio thread: this block's instantaneous count and length. A different `source` from the last call (a module's
        play mode) drops the held peak first, so one mode's peak never shows in another. */
    void publish(int countNow, int numSamples, double sampleRate, int source = 0) noexcept {
        countNow = std::max(0, countNow);
        if (source != source_) {
            source_ = source;
            peak_ = 0;
        }
        const int window = std::max(1, static_cast<int>(sampleRate * kHoldSeconds));
        if (countNow >= peak_) {
            peak_ = countNow;
            samplesLeft_ = window;
        } else {
            samplesLeft_ -= numSamples;
            if (samplesLeft_ <= 0) {
                peak_ = countNow;
                samplesLeft_ = window;
            }
        }
        value_.store(peak_, std::memory_order_relaxed);
    }

    /** Audio thread: drop the held peak at once (bypass, mute, a mode change). */
    void reset() noexcept {
        peak_ = 0;
        samplesLeft_ = 0;
        value_.store(0, std::memory_order_relaxed);
    }

    /** Message thread. */
    int value() const noexcept { return value_.load(std::memory_order_relaxed); }

private:
    int peak_ = 0;
    int source_ = 0;
    int samplesLeft_ = 0;
    std::atomic<int> value_{0};
};

} // namespace synth
