#pragma once

#include "SequencerChords.h"
#include "SequencerClock.h"
#include "SequencerPattern.h"
#include "SequencerScaleSnap.h"
#include <array>
#include <juce_audio_basics/juce_audio_basics.h>

namespace synth {

/** The MIDI channel the sequencer writes on. */
inline constexpr int kSequencerMidiChannel = 1;

/** The whole-pattern parameters, read once per block. */
struct SequencerSettings {
    bool poly = false;
    int steps = 8;
    double rateBeats = 1.0;
    int transpose = 0;
    float gateAmount = 1.0f;
    float velocityAmount = 1.0f;
    float chanceAmount = 1.0f;
};

/** Turns a pattern and a clock position into note and CC events: the step loop, chance and condition rolls, repeats,
 *  nudge, slide, chords and the notes still held across blocks. Audio thread only: no locks, no allocation. */
class SequencerPlayer {
public:
    /** Forgets every held note and the last hit without sending anything (prepareToPlay). */
    void reset() noexcept;

    /** Sends one note-off for every held note at sample 0 and forgets them (stop, bypass, transport stop). */
    void flush(juce::MidiBuffer& midi);

    bool hasHeldNotes() const noexcept { return pendingCount_ > 0; }

    /** Whether `note` is among the notes whose note-off is still to come. */
    bool isHolding(int note) const noexcept;

    /** Plays every step hit inside the block's segments, in time order, and the note-offs that fall in the block. */
    void renderBlock(const SequencerClockBlock& block, const SequencerPattern& pattern, const SequencerScaleSnap& snap,
                     const SequencerSettings& settings, juce::MidiBuffer& midi, int numSamples);

    /** Index of the step whose first hit came last (what the card highlights). */
    int activeStep() const noexcept { return activeStep_; }

private:
    struct Hit {
        int offset = 0;
        std::int64_t key = 0;
        std::int64_t step = 0;
        int index = 0;
    };
    struct Held {
        int note = 0;
        int remaining = 0;
        bool legato = false;
    };

    static constexpr int kMaxHits = 512;
    static constexpr int kMaxHeld = 256;

    void collectHits(const SequencerSegment& segment, const SequencerPattern& pattern, int steps, int numSamples);
    void playHit(const Hit& hit, const SequencerPattern& pattern, const SequencerScaleSnap& snap,
                 const SequencerSettings& settings, double samplesPerUnit, juce::MidiBuffer& midi);
    void emitDue(int upToOffset, juce::MidiBuffer& midi);
    void endBlock(int numSamples, juce::MidiBuffer& midi);
    void hold(int note, int remaining, bool legato, int offset, juce::MidiBuffer& midi);
    void removeHeld(int index) noexcept;

    std::array<Hit, kMaxHits> hits_{};
    int hitCount_ = 0;
    std::array<Held, kMaxHeld> held_{};
    int pendingCount_ = 0;
    std::int64_t lastHitKey_ = -1;
    int activeStep_ = 0;
};

} // namespace synth
