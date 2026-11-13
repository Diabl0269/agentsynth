#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @brief Remembers which MIDI input each incoming note came from, so a take can record one device only.
 *
 * The collector-merged buffer the graph plays loses the device a message came from. This keeps a second,
 * per-device copy of the note on/off messages: one lock-free lane per device identifier, written by that
 * device's driver thread and drained once per device callback into a Block of callback-relative sample
 * positions. docs/timeline/transport.md#recording-from-one-input.
 *
 * Threads: sourceFor() message thread; push() the device's MIDI driver thread (one producer per identifier);
 * setSampleRate() any thread; drainBlock() audio thread. push() and drainBlock() never lock, allocate or log.
 */
class MidiInputSources {
public:
    static constexpr int kMaxSources = 32;
    static constexpr int kLaneCapacity = 512;
    static constexpr int kMaxEventsPerBlock = 1024;
    static constexpr int kNoSource = -1;
    static constexpr double kStaleAfterMs = 1000.0; // older events are a backlog from a paused device, dropped

    /** One note on/off and the source lane it came from; samplePosition is relative to the device callback. */
    struct Event {
        int samplePosition = 0;
        int source = kNoSource;
        std::uint8_t bytes[3] = {0, 0, 0};
    };

    /** What one device callback drained; read by the render passes of that callback only. */
    struct Block {
        std::array<Event, kMaxEventsPerBlock> events;
        int count = 0;
    };

    MidiInputSources() = default;

    /** The lane for `identifier`, handed out the first time it is asked for and never recycled. Message thread.
     *  kNoSource for an empty identifier or once every lane is taken. */
    int sourceFor(const juce::String& identifier);
    /** The lane already handed out for `identifier`, or kNoSource. Any thread. */
    int findSource(const juce::String& identifier) const noexcept;

    /** Copies a note on/off from `identifier`'s device into its lane; anything else, or an unknown device, is
     *  ignored. A full lane drops the message. */
    void push(const juce::String& identifier, const juce::MidiMessage& message) noexcept;

    void setSampleRate(double sampleRate) noexcept { sampleRate_.store(sampleRate, std::memory_order_relaxed); }

    /** Empties every lane into `out`, placing each event in a callback of `numSamples` that ends at `nowMs`
     *  (juce::Time::getMillisecondCounterHiRes). */
    void drainBlock(Block& out, double nowMs, int numSamples) noexcept;

private:
    struct Entry {
        double timeMs = 0.0;
        std::uint8_t bytes[3] = {0, 0, 0};
    };
    struct Lane {
        juce::AbstractFifo fifo{kLaneCapacity};
        std::array<Entry, kLaneCapacity> entries;
    };

    static int samplePositionFor(double ageMs, double windowMs, double blockMs, int numSamples) noexcept;

    std::array<juce::String, kMaxSources> identifiers_; // written once per index, before count_ publishes it
    std::atomic<int> count_{0};
    std::array<Lane, kMaxSources> lanes_;
    std::atomic<double> sampleRate_{44100.0};
    double lastDrainMs_ = 0.0; // audio thread only

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MidiInputSources)
};

} // namespace synth
