#pragma once

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"

#include <juce_core/juce_core.h>

// DetectModeController.h -- FRO134 (docs/control/midi-remote-ui.md#detect-mode): the state and
// decision half of the panel's Detect toggle, kept apart from any component so it is headless-
// testable. The panel feeds it every activity event of the SELECTED profile's device (from its
// single RemoteEngine::drainActivity pass) and acts on what comes back; this class never touches
// the engine, the store or a component -- which is exactly how "Detect never consumes, never
// assigns, never touches the patch's MIDI flow" holds by construction.
namespace synth::ui {

class DetectModeController {
public:
    struct Step {
        bool controlAdded = false;
        juce::String litControlId; // an EXISTING control the event belongs to; empty when none
    };

    void setActive(bool active);
    bool isActive() const noexcept { return active_; }

    /** While active: a message no control on `profile` claims adds one (touch order, next free
     *  cell) and it becomes the pulsing control; an existing control's message reports it as lit and
     *  ends the pulse. Inactive: a no-op. `profile` is a working copy the caller persists once
     *  after the drain, only when a step reported controlAdded. */
    Step handleEvent(synth::ControllerProfile& profile, const synth::midi::RemoteEvent& event);

    /** The control that should pulse until the next message arrives; empty when none. */
    const juce::String& getPulsingControlId() const noexcept { return pulsingControlId_; }

private:
    bool active_ = false;
    juce::String pulsingControlId_;
};

} // namespace synth::ui
