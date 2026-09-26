// ProfileEditHistory.cpp -- the controller edit history's cursor bookkeeping. See the header;
// docs/control/midi-remote.md#undo for how it sits beside the project's AppUndoManager.

#include "MidiRemote/ProfileEditHistory.h"

#include <cassert>

namespace synth::midi {

// A new edit after an undo discards the redo tail, like every linear undo stack. Past the cap the
// OLDEST step is dropped (a std::vector erase at the front -- at most 100 profiles' worth of
// copies, on a user edit, so the shuffle is irrelevant next to the profile file write it follows).
void ProfileEditHistory::record(ProfileEditStep step) {
    steps_.erase(steps_.begin() + cursor_, steps_.end());
    steps_.push_back(std::move(step));
    if (static_cast<int>(steps_.size()) > kMaxSteps)
        steps_.erase(steps_.begin(), steps_.begin() + (static_cast<int>(steps_.size()) - kMaxSteps));
    cursor_ = static_cast<int>(steps_.size());
}

juce::String ProfileEditHistory::getUndoLabel() const { return canUndo() ? peekUndo().label : juce::String(); }

juce::String ProfileEditHistory::getRedoLabel() const { return canRedo() ? peekRedo().label : juce::String(); }

const ProfileEditStep& ProfileEditHistory::peekUndo() const {
    assert(canUndo());
    return steps_[static_cast<size_t>(cursor_ - 1)];
}

const ProfileEditStep& ProfileEditHistory::peekRedo() const {
    assert(canRedo());
    return steps_[static_cast<size_t>(cursor_)];
}

void ProfileEditHistory::commitUndo() noexcept {
    if (canUndo())
        --cursor_;
}

void ProfileEditHistory::commitRedo() noexcept {
    if (canRedo())
        ++cursor_;
}

void ProfileEditHistory::clear() noexcept {
    steps_.clear();
    cursor_ = 0;
}

} // namespace synth::midi
