// Concern: Detect mode's per-event decision (docs/control/midi-remote-ui.md#detect-mode).

#include "UI/MidiRemote/Detect/DetectModeController.h"

#include "MidiRemote/ControllerDetect.h"

namespace synth::ui {

void DetectModeController::setActive(bool active) {
    active_ = active;
    if (!active_)
        pulsingControlId_.clear();
}

DetectModeController::Step DetectModeController::handleEvent(synth::ControllerProfile& profile,
                                                             const synth::midi::RemoteEvent& event) {
    Step step;
    if (!active_)
        return step;

    if (const auto* existing = synth::midi::findControlForEvent(profile.controls, event)) {
        step.litControlId = existing->id;
        pulsingControlId_.clear(); // "pulsing until the next one arrives"
        return step;
    }

    if (!synth::midi::isDetectCandidate(event))
        return step;

    auto control = synth::midi::makeDetectedControl(event, profile.controls);
    pulsingControlId_ = control.id;
    profile.controls.push_back(std::move(control));
    step.controlAdded = true;
    return step;
}

} // namespace synth::ui
