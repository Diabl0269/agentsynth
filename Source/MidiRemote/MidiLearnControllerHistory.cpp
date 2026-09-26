// Concern: FRO273 -- the controller edit history (docs/control/midi-remote.md#undo). Recording a
// profile edit's before/after state from inside every profile mutation, and applying a recorded
// state back through the SAME mutation path on undo/redo, so the engine snapshot is republished
// and onChanged refreshes the panel exactly as for the original edit.

#include "MidiRemote/MidiLearnController.h"

#include "UI/Chrome/StatusBarComponent.h"
#include <algorithm>

namespace synth::midi {

std::optional<ControllerProfile> MidiLearnController::profileSnapshot(const juce::String& id) const {
    if (const auto* profile = findProfile(id))
        return *profile;
    return std::nullopt;
}

// Called AFTER the mutation, so the after-state is read straight from profiles_ -- one call site
// shape for add/update/delete alike. Suppressed while an undo/redo applies a recorded state (those
// run through the same mutation methods, and must not record themselves). A no-op edit (a drag
// dropped back on its own cell, a rename to the same name) records nothing: ControllerProfile has
// no operator==, so the JSON documents are compared instead.
void MidiLearnController::recordProfileEdit(const juce::String& label, const juce::String& profileId,
                                            std::optional<ControllerProfile> before) {
    if (applyingProfileHistory_)
        return;
    auto after = profileSnapshot(profileId);
    if (before.has_value() == after.has_value()) {
        if (!before.has_value())
            return;
        if (juce::JSON::toString(before->toVar()) == juce::JSON::toString(after->toVar()))
            return;
    }
    profileHistory_.record({label, profileId, std::move(before), std::move(after)});
}

bool MidiLearnController::undoProfileEdit() {
    if (!profileHistory_.canUndo())
        return false;
    const auto step = profileHistory_.peekUndo(); // a copy: applying may re-enter onChanged
    if (!applyProfileState(step.profileId, step.before))
        return false;
    profileHistory_.commitUndo();
    statusBar_.showMessage("Undo " + step.label);
    if (onChanged)
        onChanged(); // the cursor moved after the mutation's own notification
    return true;
}

bool MidiLearnController::redoProfileEdit() {
    if (!profileHistory_.canRedo())
        return false;
    const auto step = profileHistory_.peekRedo();
    if (!applyProfileState(step.profileId, step.after))
        return false;
    profileHistory_.commitRedo();
    statusBar_.showMessage("Redo " + step.label);
    if (onChanged)
        onChanged(); // the cursor moved after the mutation's own notification
    return true;
}

// Re-add, update or delete, whichever turns the live set into `state`. Deleting a profile leaves
// its project assignments orphaned exactly as the panel's own Delete controller does; restoring it
// relinks them for free, since they reference the profile by id. The cursor only moves (in the
// callers above) once this succeeded, so a refused restore leaves the history where it was.
bool MidiLearnController::applyProfileState(const juce::String& profileId,
                                            const std::optional<ControllerProfile>& state) {
    const juce::ScopedValueSetter<bool> suppress(applyingProfileHistory_, true);
    if (!state.has_value())
        return findProfile(profileId) == nullptr || deleteProfile(profileId);

    const bool ok = findProfile(profileId) != nullptr ? updateProfile(*state) : addProfile(*state);
    if (ok)
        resyncAssignmentCopies(*state);
    return ok;
}

// updateControl() rewrites the denormalised name/encoding/button mode onto every PROJECT
// assignment in place, outside any undo step. Restoring an older profile state would otherwise
// leave those copies describing the undone edit -- and the engine decodes from the assignment's
// copy, not the profile. Same in-place write, same "not a project undo step" rule.
void MidiLearnController::resyncAssignmentCopies(const ControllerProfile& profile) {
    bool changed = false;
    for (auto& a : doc_.assignments) {
        if (a.control.profileId != profile.id)
            continue;
        const auto it = std::find_if(profile.controls.begin(), profile.controls.end(),
                                     [&](const Control& c) { return c.id == a.control.controlId; });
        if (it == profile.controls.end())
            continue;
        if (a.specControlName == it->name && a.specEncoding == it->encoding && a.specButtonMode == it->buttonMode)
            continue;
        a.specControlName = it->name;
        a.specEncoding = it->encoding;
        a.specButtonMode = it->buttonMode;
        changed = true;
    }
    if (changed)
        publishAssignments();
}

} // namespace synth::midi
