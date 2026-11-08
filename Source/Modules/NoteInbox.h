#pragma once

// Notes one module hands straight to another module's MIDI input, with no cable: a Drum Kit's instrument pads post
// here and the receiving module's processBlock drains them first. Lock-free on both sides.

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace synth {

class NoteInbox : public juce::ReferenceCountedObject {
public:
    using Ptr = juce::ReferenceCountedObjectPtr<NoteInbox>;
    static constexpr int kCapacity = 64;
    static constexpr int kMaxPerBlock = 32;

    /** Any thread, no allocation. A note-on (velocity 1..127) or a note-off (velocity 0) `offset` samples into the
        sender's block. A note-off that finds the queue busy or full still arrives, at the end of the next block. */
    virtual void push(int pitch, int velocity, int offset) noexcept;
    /** The receiver's audio thread: moves up to kMaxPerBlock queued notes into `midi`, at their offsets. */
    void drainInto(juce::MidiBuffer& midi, int numSamples) noexcept;

private:
    struct Event {
        std::uint8_t pitch = 0;
        std::uint8_t velocity = 0;
        int offset = 0;
    };

    void markLostOff(int pitch) noexcept;

    juce::AbstractFifo fifo_{kCapacity};
    std::array<Event, kCapacity> events_{};
    juce::SpinLock writeLock_;
    std::array<std::atomic<std::uint64_t>, 2> lostOffs_{};
};

/** What a pad plays when the module it names is one of several that a track feeds its MIDI to (an Oscillator, the
    envelope that gates it, ...): a push reaches every target, so the pad sounds the way the track does. Targets are
    fixed at construction; a changed set makes a new fan. */
class NoteFan : public NoteInbox {
public:
    static constexpr int kMaxTargets = 8;

    explicit NoteFan(const std::vector<NoteInbox*>& targets);
    void push(int pitch, int velocity, int offset) noexcept override;
    /** True when `other` reaches exactly the same inboxes in the same order. */
    bool sameTargets(const NoteFan& other) const noexcept;

private:
    std::array<NoteInbox::Ptr, kMaxTargets> targets_;
    int count_ = 0;
};

} // namespace synth
