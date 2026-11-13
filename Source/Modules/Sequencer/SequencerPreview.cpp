#include "SequencerPreview.h"
#include "SequencerPlayer.h"
#include <algorithm>
#include <cmath>

namespace synth {

namespace {
constexpr double kPreviewSeconds = 0.3;
} // namespace

void SequencerPreview::prepare(double sampleRate) noexcept {
    lengthSamples_ = std::max(1, (int)std::lround(kPreviewSeconds * sampleRate));
    soundingCount_ = 0;
    remaining_ = 0;
    seenSequence_ = sequence_.load(std::memory_order_acquire) & ~1u;
}

// A seqlock: the writer makes the counter odd, fills the fields, then makes it even again. Every field is atomic, so a
// reader that sees the counter change underneath it simply throws the copy away and tries again next block.
void SequencerPreview::request(const int* notes, int count, float velocity) noexcept {
    const int n = notes == nullptr ? 0 : std::clamp(count, 0, kMaxNotes);
    const auto start = sequence_.load(std::memory_order_relaxed);
    sequence_.store(start + 1, std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_release);
    for (int i = 0; i < n; ++i)
        requestNotes_[(size_t)i].store(std::clamp(notes[i], 0, 127), std::memory_order_relaxed);
    requestCount_.store(n, std::memory_order_relaxed);
    requestVelocity_.store(std::clamp((int)std::lround(velocity * 127.0f), 1, 127), std::memory_order_relaxed);
    sequence_.store(start + 2, std::memory_order_release);
}

bool SequencerPreview::takeRequest(std::array<int, kMaxNotes>& notes, int& count, int& velocity) noexcept {
    const auto before = sequence_.load(std::memory_order_acquire);
    if ((before & 1u) != 0 || before == seenSequence_)
        return false;
    count = std::clamp(requestCount_.load(std::memory_order_relaxed), 0, kMaxNotes);
    velocity = requestVelocity_.load(std::memory_order_relaxed);
    for (int i = 0; i < count; ++i)
        notes[(size_t)i] = requestNotes_[(size_t)i].load(std::memory_order_relaxed);
    std::atomic_thread_fence(std::memory_order_acquire);
    if (sequence_.load(std::memory_order_relaxed) != before)
        return false;
    seenSequence_ = before;
    return true;
}

// The pattern may be holding the same pitch (its own note-off will end it), so the preview never cuts that note.
void SequencerPreview::endSounding(juce::MidiBuffer& midi, int offset, const SequencerPlayer& player) noexcept {
    for (int i = 0; i < soundingCount_; ++i)
        if (!player.isHolding(sounding_[(size_t)i]))
            midi.addEvent(juce::MidiMessage::noteOff(kSequencerMidiChannel, sounding_[(size_t)i]), offset);
    soundingCount_ = 0;
}

void SequencerPreview::render(juce::MidiBuffer& midi, int numSamples, const SequencerPlayer& player) noexcept {
    std::array<int, kMaxNotes> notes{};
    int count = 0;
    int velocity = 100;
    if (takeRequest(notes, count, velocity)) {
        endSounding(midi, 0, player);
        for (int i = 0; i < count; ++i) {
            midi.addEvent(juce::MidiMessage::noteOn(kSequencerMidiChannel, notes[(size_t)i], (juce::uint8)velocity), 0);
            sounding_[(size_t)i] = notes[(size_t)i];
        }
        soundingCount_ = count;
        remaining_ = lengthSamples_;
    }
    if (soundingCount_ == 0)
        return;
    if (remaining_ < numSamples)
        endSounding(midi, std::max(0, remaining_), player);
    else
        remaining_ -= numSamples;
}

void SequencerPreview::silence(juce::MidiBuffer& midi, const SequencerPlayer& player) noexcept {
    std::array<int, kMaxNotes> notes{};
    int count = 0;
    int velocity = 0;
    takeRequest(notes, count, velocity);
    endSounding(midi, 0, player);
}

} // namespace synth
