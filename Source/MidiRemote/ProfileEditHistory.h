#pragma once

#include "MidiRemote/RemoteModel.h"
#include <juce_core/juce_core.h>
#include <optional>
#include <vector>

// ProfileEditHistory.h -- the controller edit history (docs/control/midi-remote.md#undo): one
// in-memory undo/redo list shared by every ControllerProfile, separate from the project's
// AppUndoManager. Pure data -- MidiLearnController records into it and applies its states.

namespace synth::midi {

/** One recorded profile edit. `before` empty = the edit created the profile; `after` empty = it
 *  deleted it. */
struct ProfileEditStep {
    juce::String label;
    juce::String profileId;
    std::optional<ControllerProfile> before;
    std::optional<ControllerProfile> after;
};

class ProfileEditHistory final {
public:
    static constexpr int kMaxSteps = 100;

    /** Appends `step`, dropping any redo tail and the oldest step past kMaxSteps. */
    void record(ProfileEditStep step);

    bool canUndo() const noexcept { return cursor_ > 0; }
    bool canRedo() const noexcept { return cursor_ < static_cast<int>(steps_.size()); }
    /** Empty when canUndo()/canRedo() is false. */
    juce::String getUndoLabel() const;
    juce::String getRedoLabel() const;

    /** The step undo/redo would apply next. Precondition: canUndo() / canRedo(). */
    const ProfileEditStep& peekUndo() const;
    const ProfileEditStep& peekRedo() const;
    /** Moves the cursor past the peeked step once the caller has applied it. */
    void commitUndo() noexcept;
    void commitRedo() noexcept;

    void clear() noexcept;
    int getNumSteps() const noexcept { return static_cast<int>(steps_.size()); }

private:
    std::vector<ProfileEditStep> steps_;
    int cursor_ = 0; // steps_[0, cursor_) are undoable, steps_[cursor_, size) redoable
};

} // namespace synth::midi
