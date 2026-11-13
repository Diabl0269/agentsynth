// HeldNotes.h -- a fixed-size stack of the MIDI notes currently held, newest last. Header-only and
// free of JUCE types so a voice can ask "is another note still down?" on the audio thread (no
// allocation, no locks) and unit tests can drive it directly.
#pragma once

#include <array>

namespace synth {

class HeldNotes {
public:
    static constexpr int kCapacity = 16;

    void clear() noexcept { count_ = 0; }

    bool empty() const noexcept { return count_ == 0; }
    int size() const noexcept { return count_; }

    /** True when a held note other than `note` is down. */
    bool anyOtherThan(int note) const noexcept {
        for (int i = 0; i < count_; ++i)
            if (notes_[(size_t)i] != note)
                return true;
        return false;
    }

    /** True when a held note other than `note` satisfies `counts(heldNote)`. */
    template <class Predicate>
    bool anyOtherThanIf(int note, Predicate counts) const {
        for (int i = 0; i < count_; ++i)
            if (notes_[(size_t)i] != note && counts(notes_[(size_t)i]))
                return true;
        return false;
    }

    /** The most recently pressed note still held, or -1. */
    int newest() const noexcept { return count_ > 0 ? notes_[(size_t)count_ - 1] : -1; }

    /** Presses `note`: a note already held moves to the top; when the stack is full the oldest
        note is dropped to make room. */
    void press(int note) noexcept {
        release(note);
        if (count_ == kCapacity)
            removeAt(0);
        notes_[(size_t)count_++] = note;
    }

    /** Releases `note`; a note that is not held is ignored. */
    void release(int note) noexcept {
        for (int i = 0; i < count_; ++i) {
            if (notes_[(size_t)i] == note) {
                removeAt(i);
                return;
            }
        }
    }

private:
    void removeAt(int index) noexcept {
        for (int i = index; i + 1 < count_; ++i)
            notes_[(size_t)i] = notes_[(size_t)i + 1];
        --count_;
    }

    std::array<int, kCapacity> notes_{};
    int count_ = 0;
};

} // namespace synth
