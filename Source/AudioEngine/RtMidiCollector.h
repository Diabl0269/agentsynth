#pragma once

// A lock-free stand-in for juce::MidiMessageCollector on the one path that runs on the render thread:
// a MIDI driver thread pushes short messages, the audio thread takes the next block's worth. The JUCE
// collector guards its queue with a CriticalSection and grows a MidiBuffer under it, both reported by the
// run-rtsan job; this one hands the messages over through a pre-allocated SpscRing and places them
// with the JUCE collector's own arithmetic, so a message lands at the same sample it always did.
//
// Placement, as in juce::MidiMessageCollector::removeNextBlockOfMessages: a message's position is its
// timestamp minus the previous call's time, in samples. If the time since the previous call spans fewer
// samples than the block, the messages sit at the END of the block; if more, they are scaled down to fit
// (at most 32 blocks of history are kept); a position outside the block is clamped into it.
//
// Only messages of up to 3 bytes are carried (every channel message and the realtime bytes); a longer one
// (SysEx) is refused by push(), which returns false: nothing in the graph consumes SysEx.
//
// Threads: push() from one producer at a time (a device's MIDI callback); removeNextBlockOfMessages(),
// discardNextBlock() from the render thread; reset() from the message thread while the audio is stopped.

#include "AudioEngine/SpscRing.h"
#include <algorithm>
#include <atomic>
#include <cstdint>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

namespace synth {

class RtMidiCollector {
public:
    static constexpr int kCapacity = 1024;

    RtMidiCollector()
        : ring(kCapacity) {
        scratch.reserve(kCapacity);
    }

    /** Call before use and on a sample-rate change; forgets everything queued. */
    void reset(double newSampleRate) {
        sampleRate.store(newSampleRate, std::memory_order_relaxed);
        ring.drain([](const Slot&) {});
        lastCallbackTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
    }

    /** Any thread, one at a time. Never allocates or blocks. False when the message is dropped (too long or full). */
    bool push(const juce::MidiMessage& message) noexcept {
        const int numBytes = message.getRawDataSize();
        if (numBytes < 1 || numBytes > 3)
            return false;
        Slot slot;
        slot.timeStamp = message.getTimeStamp();
        slot.numBytes = static_cast<std::uint8_t>(numBytes);
        const auto* raw = message.getRawData();
        for (int i = 0; i < numBytes; ++i)
            slot.data[i] = raw[i];
        return ring.push(slot);
    }

    /** Render thread. Adds the queued messages to `dest` (channel 0 = all, else only that MIDI channel). */
    void removeNextBlockOfMessages(juce::MidiBuffer& dest, int numSamples, int channelFilter = 0) {
        const double rate = sampleRate.load(std::memory_order_relaxed);
        const double previousCallbackMs = lastCallbackTimeMs.load(std::memory_order_relaxed);
        const double timeNow = juce::Time::getMillisecondCounterHiRes();
        const double msElapsed = timeNow - previousCallbackMs;
        lastCallbackTimeMs.store(timeNow, std::memory_order_relaxed);

        scratch.clear();
        ring.drain([&](const Slot& s) {
            if (scratch.size() < scratch.capacity())
                scratch.push_back(s);
        });
        if (scratch.empty() || numSamples <= 0)
            return;

        // Sample position of every message relative to the previous call.
        for (auto& s : scratch)
            s.position = static_cast<int>((s.timeStamp - 0.001 * previousCallbackMs) * rate);

        int numSourceSamples = std::max(1, juce::roundToInt(msElapsed * 0.001 * rate));
        int startSample = 0;
        int scale = 1 << 16;
        const bool squeeze = numSourceSamples > numSamples;
        if (squeeze) {
            const int maxBlockLengthToUse = numSamples << 5;
            if (numSourceSamples > maxBlockLengthToUse) {
                startSample = numSourceSamples - maxBlockLengthToUse;
                numSourceSamples = maxBlockLengthToUse;
            }
            scale = (numSamples << 10) / numSourceSamples;
        } else {
            startSample = numSamples - numSourceSamples;
        }

        for (const auto& s : scratch) {
            if (channelFilter > 0 && channelOf(s) != channelFilter)
                continue;
            int pos;
            if (squeeze) {
                if (s.position < startSample)
                    continue;
                pos = ((s.position - startSample) * scale) >> 10;
            } else {
                pos = s.position + startSample;
            }
            dest.addEvent(s.data, s.numBytes, juce::jlimit(0, numSamples - 1, pos));
        }
    }

    /** Render thread: the same bookkeeping as removeNextBlockOfMessages, with the messages thrown away. */
    void discardNextBlock() {
        lastCallbackTimeMs.store(juce::Time::getMillisecondCounterHiRes(), std::memory_order_relaxed);
        ring.drain([](const Slot&) {});
    }

private:
    struct Slot {
        double timeStamp = 0.0;
        std::uint8_t data[3] = {0, 0, 0};
        std::uint8_t numBytes = 0;
        int position = 0; // filled in on the render thread
    };

    // The MIDI channel (1-16) of a channel message, 0 for a system message.
    static int channelOf(const Slot& s) noexcept {
        const int status = s.data[0];
        return status >= 0x80 && status < 0xf0 ? (status & 0x0f) + 1 : 0;
    }

    SpscRing<Slot> ring;
    std::vector<Slot> scratch; // render thread's copy of one block's messages, reserved once
    std::atomic<double> sampleRate{44100.0};
    std::atomic<double> lastCallbackTimeMs{0.0};
};

} // namespace synth
