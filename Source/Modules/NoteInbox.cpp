// NoteInbox.cpp -- the queue behind a cable-free note: senders take a try-lock (two Drum Kits can play one module),
// the receiver reads without one. A lost note-on is dropped; a lost note-off is kept as a bit per pitch so no note
// is left hanging.
#include "Modules/NoteInbox.h"
#include "Modules/ModuleBase.h"

namespace synth {

void NoteInbox::markLostOff(int pitch) noexcept {
    lostOffs_[(size_t)(pitch >> 6)].fetch_or(std::uint64_t{1} << (unsigned)(pitch & 63), std::memory_order_acq_rel);
}

void NoteInbox::push(int pitch, int velocity, int offset) noexcept {
    pitch = juce::jlimit(0, 127, pitch);
    velocity = juce::jlimit(0, 127, velocity);
    const juce::SpinLock::ScopedTryLockType lock(writeLock_);
    if (!lock.isLocked() || fifo_.getFreeSpace() < 1) {
        if (velocity == 0)
            markLostOff(pitch);
        return;
    }
    const auto scope = fifo_.write(1);
    const int slot = scope.blockSize1 > 0 ? scope.startIndex1 : scope.startIndex2;
    events_[(size_t)slot] = {(std::uint8_t)pitch, (std::uint8_t)velocity, juce::jmax(0, offset)};
}

NoteFan::NoteFan(const std::vector<NoteInbox*>& targets) {
    for (auto* target : targets)
        if (target != nullptr && count_ < kMaxTargets)
            targets_[(size_t)count_++] = target;
}

void NoteFan::push(int pitch, int velocity, int offset) noexcept {
    for (int i = 0; i < count_; ++i)
        targets_[(size_t)i]->push(pitch, velocity, offset);
}

bool NoteFan::sameTargets(const NoteFan& other) const noexcept {
    if (count_ != other.count_)
        return false;
    for (int i = 0; i < count_; ++i)
        if (targets_[(size_t)i].get() != other.targets_[(size_t)i].get())
            return false;
    return true;
}

// The graph sizes every MIDI buffer before playback, so a bounded number of adds per block never allocates.
void NoteInbox::drainInto(juce::MidiBuffer& midi, int numSamples) noexcept {
    const int last = juce::jmax(0, numSamples - 1);
    const int count = juce::jmin(fifo_.getNumReady(), kMaxPerBlock);
    if (count > 0) {
        const auto scope = fifo_.read(count);
        scope.forEach([&](int index) {
            const auto& e = events_[(size_t)index];
            const int at = juce::jmin(e.offset, last);
            if (e.velocity > 0)
                midi.addEvent(juce::MidiMessage::noteOn(1, e.pitch, (juce::uint8)e.velocity), at);
            else
                midi.addEvent(juce::MidiMessage::noteOff(1, e.pitch), at);
        });
    }
    for (int word = 0; word < 2; ++word) {
        auto bits = lostOffs_[(size_t)word].exchange(0, std::memory_order_acq_rel);
        for (int bit = 0; bits != 0; ++bit, bits >>= 1)
            if ((bits & 1u) != 0)
                midi.addEvent(juce::MidiMessage::noteOff(1, word * 64 + bit), last);
    }
}

} // namespace synth

// Made on the message thread and published by an atomic pointer the render reads once per block; the module owns it
// and a sender holds its own reference, so a deleted module's inbox outlives any pad still pointing at it.
synth::NoteInbox& ModuleBase::noteInbox() {
    if (noteInboxOwner_ == nullptr) {
        noteInboxOwner_ = new synth::NoteInbox();
        noteInbox_.store(noteInboxOwner_.get(), std::memory_order_release);
    }
    return *noteInboxOwner_;
}
