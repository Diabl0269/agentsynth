#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

class SequencerPlayer;

/** A short audition chord the UI asks the sequencer to play, independent of the pattern and of Run. The message
 *  thread requests; the audio thread turns the newest request into note-ons at the start of its next block and
 *  note-offs a fixed time later. Lock-free and allocation-free on both sides. */
class SequencerPreview {
public:
    static constexpr int kMaxNotes = 8;

    /** Audio thread, in prepareToPlay: sets the note length for the sample rate and forgets what was sounding. */
    void prepare(double sampleRate) noexcept;

    /** Message thread only (one writer): replaces any earlier request with these notes (at most kMaxNotes, 0..127 each;
     *  a null or empty set ends the preview). `velocity` is 0..1. */
    void request(const int* notes, int count, float velocity) noexcept;

    /** Audio thread, once per block after the pattern rendered: ends or replaces the sounding preview as asked, starts
     * a new one, and ends it when its time is up. Leaves a note on if `player` is holding the same pitch. */
    void render(juce::MidiBuffer& midi, int numSamples, const SequencerPlayer& player) noexcept;

    /** Audio thread: drops any request and ends what is sounding (bypass). */
    void silence(juce::MidiBuffer& midi, const SequencerPlayer& player) noexcept;

private:
    void endSounding(juce::MidiBuffer& midi, int offset, const SequencerPlayer& player) noexcept;
    bool takeRequest(std::array<int, kMaxNotes>& notes, int& count, int& velocity) noexcept;

    // The request slot: written between two bumps of sequence_ (odd while being written), read by retrying next block.
    std::atomic<std::uint32_t> sequence_{0};
    std::array<std::atomic<int>, kMaxNotes> requestNotes_{};
    std::atomic<int> requestCount_{0};
    std::atomic<int> requestVelocity_{100};

    std::uint32_t seenSequence_ = 0;        // audio thread only
    std::array<int, kMaxNotes> sounding_{}; // audio thread only
    int soundingCount_ = 0;                 // audio thread only
    int remaining_ = 0;                     // audio thread only
    int lengthSamples_ = 13230;             // audio thread only
};

} // namespace synth
