// Concern: the per-device MIDI lanes recording filters on -- handing out a lane per device identifier, the MIDI
// driver thread's push, and the audio thread's once-per-callback drain into callback-relative positions.
#include "MidiInputSources.h"

#include <algorithm>
#include <cmath>

namespace synth {

// A lane index is assigned once and never recycled (the MIDI Remote lane rule), so a lane never gains a second
// producer: a device that goes away and comes back with the same identifier gets the same lane. The identifier is
// written before count_ is released, so a reader that acquires count_ sees every string below it complete and
// never written again.
int MidiInputSources::sourceFor(const juce::String& identifier) {
    if (identifier.isEmpty())
        return kNoSource;
    if (const int existing = findSource(identifier); existing != kNoSource)
        return existing;
    const int index = count_.load(std::memory_order_relaxed);
    if (index >= kMaxSources)
        return kNoSource;
    identifiers_[static_cast<std::size_t>(index)] = identifier;
    count_.store(index + 1, std::memory_order_release);
    return index;
}

int MidiInputSources::findSource(const juce::String& identifier) const noexcept {
    if (identifier.isEmpty())
        return kNoSource;
    const int count = count_.load(std::memory_order_acquire);
    for (int i = 0; i < count; ++i)
        if (identifiers_[static_cast<std::size_t>(i)] == identifier)
            return i;
    return kNoSource;
}

// Only note on/off are kept: recording reads nothing else, and a 3-byte message is checked on its raw bytes so
// no juce::MidiMessage is built. The timestamp is the driver's (seconds on the millisecond-counter clock), the
// same one juce::MidiMessageCollector places the merged stream by.
void MidiInputSources::push(const juce::String& identifier, const juce::MidiMessage& message) noexcept {
    if (message.getRawDataSize() != 3)
        return;
    const auto* data = message.getRawData();
    const int status = data[0] & 0xf0;
    if (status != 0x80 && status != 0x90)
        return;
    const int source = findSource(identifier);
    if (source == kNoSource)
        return;

    auto& lane = lanes_[static_cast<std::size_t>(source)];
    int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
    lane.fifo.prepareToWrite(1, start1, size1, start2, size2);
    if (size1 + size2 < 1)
        return;
    auto& entry = lane.entries[static_cast<std::size_t>(start1)];
    entry.timeMs = message.getTimeStamp() * 1000.0;
    std::copy(data, data + 3, entry.bytes);
    lane.fifo.finishedWrite(1);
}

// Drained on every device callback, recording or not, so a take never starts with notes played before it. The
// window is the time since the previous drain: within one block's length an event lands where the collector
// would put it (counting back from the block's end); a longer gap is squeezed into the block, as the collector
// does, so nothing is lost.
void MidiInputSources::drainBlock(Block& out, double nowMs, int numSamples) noexcept {
    out.count = 0;
    const double rate = sampleRate_.load(std::memory_order_relaxed);
    const double blockMs = rate > 0.0 ? 1000.0 * numSamples / rate : 0.0;
    const double windowMs = lastDrainMs_ > 0.0 ? nowMs - lastDrainMs_ : blockMs;
    lastDrainMs_ = nowMs;

    const int count = count_.load(std::memory_order_acquire);
    for (int source = 0; source < count; ++source) {
        auto& lane = lanes_[static_cast<std::size_t>(source)];
        const int ready = lane.fifo.getNumReady();
        if (ready <= 0)
            continue;
        int start1 = 0, size1 = 0, start2 = 0, size2 = 0;
        lane.fifo.prepareToRead(ready, start1, size1, start2, size2);
        const auto take = [&](int start, int size) {
            for (int i = 0; i < size; ++i) {
                const auto& entry = lane.entries[static_cast<std::size_t>(start + i)];
                if (nowMs - entry.timeMs > kStaleAfterMs || out.count >= kMaxEventsPerBlock)
                    continue;
                auto& event = out.events[static_cast<std::size_t>(out.count++)];
                event.samplePosition = samplePositionFor(nowMs - entry.timeMs, windowMs, blockMs, numSamples);
                event.source = source;
                std::copy(entry.bytes, entry.bytes + 3, event.bytes);
            }
        };
        take(start1, size1);
        take(start2, size2);
        lane.fifo.finishedRead(size1 + size2);
    }
}

int MidiInputSources::samplePositionFor(double ageMs, double windowMs, double blockMs, int numSamples) noexcept {
    if (numSamples <= 0 || !(blockMs > 0.0))
        return 0;
    const double span = std::max(windowMs, blockMs); // a longer gap than one block is squeezed into it
    const double samplesBack = std::max(0.0, ageMs) / span * numSamples;
    return std::clamp(numSamples - static_cast<int>(std::lround(samplesBack)), 0, numSamples - 1);
}

} // namespace synth
