#pragma once

// Small helpers shared by the polyphonic MIDI instruments that run their own voices (Pluck, Mallet):
// MIDI note to Hz, the quiet-voice detector, and the "which voice do I steal" rule.

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace synth::voices {

/** 12-TET frequency of a MIDI note (A4 = note 69 = 440 Hz), shifted by `tuneSemitones` and `fineCents`. */
inline double noteHz(int note, float tuneSemitones, float fineCents) noexcept {
    return 440.0 * std::exp2((static_cast<double>(note) - 69.0 + tuneSemitones + fineCents / 100.0) / 12.0);
}

/** An output below this peak (-100 dB) counts as silence. */
inline constexpr float kQuiet = 1.0e-5f;

/** Reports a voice as finished once a whole window of its output stays below kQuiet. */
struct PeakWatch {
    float windowPeak = 0.0f;
    int count = 0;
    int window = 1024;

    void reset(int windowSamples) noexcept {
        windowPeak = 0.0f;
        count = 0;
        window = std::max(windowSamples, 16);
    }
    /** Feed one output sample; true when the window that just closed was quiet. */
    bool quietAfter(float sample) noexcept {
        windowPeak = std::max(windowPeak, std::fabs(sample));
        if (++count < window)
            return false;
        const bool quiet = windowPeak < kQuiet;
        windowPeak = 0.0f;
        count = 0;
        return quiet;
    }
};

/** Index of the voice to (re)use for a new note: a free one, else the oldest already released, else the
    oldest overall. Voices need `active`, `held` and `serial` (a number that grows with each note-on). */
template <typename VoiceArray>
std::size_t pickVoice(const VoiceArray& voices) noexcept {
    std::size_t best = 0;
    int bestClass = 3;
    std::uint32_t bestSerial = 0;
    for (std::size_t i = 0; i < voices.size(); ++i) {
        const auto& v = voices[i];
        const int cls = !v.active ? 0 : (!v.held ? 1 : 2);
        if (cls < bestClass || (cls == bestClass && v.serial < bestSerial)) {
            best = i;
            bestClass = cls;
            bestSerial = v.serial;
        }
    }
    return best;
}

} // namespace synth::voices
