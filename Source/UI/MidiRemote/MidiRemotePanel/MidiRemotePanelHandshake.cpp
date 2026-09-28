// Concern: FRO339 (docs/control/midi-remote-device-handshake.md#device-handshake) -- the toolbar's
// port-hint warning row for whichever controller is currently selected, mirroring
// MidiRemotePanelUndo.cpp's own "re-derive a toolbar hint after every change" shape.
#include "MidiRemotePanelComponent.h"

#include "MidiRemote/MidiLearnController.h"

namespace synth::ui {

void MidiRemotePanelComponent::refreshPortHint() {
    juce::String text;
    if (learnController_ != nullptr && !selectedProfileId_.isEmpty())
        text = learnController_->getHandshakeIssueForProfile(selectedProfileId_);
    if (text == toolbar_.getPortHint())
        return;
    toolbar_.setPortHint(text);
    resized(); // the hint row changes the toolbar's height -- same as setDetectActive()'s own comment
}

} // namespace synth::ui
