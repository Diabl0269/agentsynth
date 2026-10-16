// CustomFilterVoiceCursors.h -- the per-voice morph cursors behind the Custom filter's per-note modes:
// spots each voice's new notes from its Pitch input and its audio, picks the note's shape (by hand, by
// pitch or at random) and glides the voice's cursor there. Pure control-rate logic, no DSP; audio thread.
#pragma once

#include "CustomFilterPerNote.h"
#include <array>
#include <juce_core/juce_core.h>

namespace synth {

class CustomFilterVoiceCursors {
public:
    static constexpr int kMaxVoices = 8;

    /** One voice's view of the block, read by the module before filtering. */
    struct VoiceInput {
        float pitchStartHz = 0.0f; // the voice's Pitch at the block's first sample (<= 0: no pitch)
        float pitchEndHz = 0.0f;   // ... and at its last
        bool audible = false;      // the voice's input carries sound this block
    };

    /** The shape positions the cursors aim at; `orderByX` lists shape indices left to right. */
    struct ShapeMap {
        int count = 0;
        const float* x = nullptr;
        const float* y = nullptr;
        const int* orderByX = nullptr;
    };

    struct Settings {
        CustomFilterPerNote::Mode mode = CustomFilterPerNote::Mode::Off;
        float smoothness = 0.0f;      // 0..1
        const int8_t* keys = nullptr; // CustomFilterPerNote::kNumKeys entries (By hand)
    };

    /** Allocates nothing; call from prepareToPlay. */
    void prepare(double sampleRate) noexcept;
    /** Every voice back to "no note yet", following the shared cursor. Audio thread. */
    void reset() noexcept;
    /** Advances one block of `numSamples` for `numVoices` (<= kMaxVoices) voices. `sharedX/Y` is the Morph X/Y
     *  cursor, used by voices with no note yet and notes with no shape. No allocation, locks or system calls. */
    void update(const VoiceInput* in, int numVoices, int numSamples, const Settings& settings, const ShapeMap& map,
                float sharedX, float sharedY) noexcept;

    /** True once voice `v` has had a note since the last reset(). */
    bool isEngaged(int v) const noexcept { return voices_[(size_t)v].engaged; }
    /** True when voice `v` started a note in the last update(). */
    bool noteStarted(int v) const noexcept { return voices_[(size_t)v].noteStarted; }
    /** True when voice `v`'s input carried sound in the last update(). */
    bool isAudible(int v) const noexcept { return voices_[(size_t)v].wasAudible; }
    /** Voice `v`'s cursor after the last update(). */
    float getX(int v) const noexcept { return voices_[(size_t)v].x; }
    float getY(int v) const noexcept { return voices_[(size_t)v].y; }
    /** The shape voice `v`'s current note aims at, or -1 for the shared cursor. */
    int getShape(int v) const noexcept { return voices_[(size_t)v].shape; }

    /** MIDI note nearest `hz` (> 0). */
    static int noteFromHz(float hz) noexcept;
    /** The shape By pitch gives `note` among `count` shapes ordered by `orderByX`. */
    static int shapeByPitch(int note, int count, const int* orderByX) noexcept;

private:
    struct Voice {
        bool engaged = false;
        bool noteStarted = false;
        bool wasAudible = false;
        bool pendingOnset = false;
        float noteHz = 0.0f; // the pitch the current note was spotted at
        int shape = -1;
        float startX = 0.5f, startY = 0.5f;
        float x = 0.5f, y = 0.5f;
        double elapsed = 0.0; // samples since the note started
    };

    bool detectNote(Voice& v, const VoiceInput& in, int& note) noexcept;
    int pickShape(const Voice& v, int note, const Settings& settings, const ShapeMap& map) noexcept;

    std::array<Voice, kMaxVoices> voices_{};
    double sampleRate_ = 44100.0;
    juce::Random random_{0x5eed};
};

} // namespace synth
