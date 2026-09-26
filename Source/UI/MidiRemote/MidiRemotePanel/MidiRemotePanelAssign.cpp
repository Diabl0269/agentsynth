// Concern: FRO135 -- assigning the selected control from the panel side
// (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn): the Assign... /
// Learn target menu, the pick-target overlay entry and the action picker.
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemotePanelComponent.h"
#include "UI/MidiRemote/ActionPicker/ActionPickerComponent.h"

namespace synth::ui {

void MidiRemotePanelComponent::showAssignMenu(juce::Component& anchor) {
    if (selectedControlId_.isEmpty())
        return;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::Component::SafePointer<juce::Component> safeAnchor(&anchor);

    juce::PopupMenu menu;
    menu.addItem("Pick a module control", [safeThis] {
        if (safeThis != nullptr)
            safeThis->startPickTargetForSelectedControl();
    });
    menu.addItem("Choose an action...", [safeThis, safeAnchor] {
        if (safeThis != nullptr && safeAnchor != nullptr)
            safeThis->showActionPicker(*safeAnchor);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

bool MidiRemotePanelComponent::startPickTargetForSelectedControl() {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty() || selectedControlId_.isEmpty())
        return false;
    return learnController_->beginPickTarget(selectedProfileId_, selectedControlId_);
}

void MidiRemotePanelComponent::showActionPicker(juce::Component& anchor) {
    if (selectedControlId_.isEmpty())
        return;
    auto picker = std::make_unique<ActionPickerComponent>();
    // FRO142 (docs/control/midi-remote.md#pages): the "Page N" rows must match the selected
    // control's OWN controller, set before the picker is shown -- ActionPickerComponent's own
    // header comment on why this can't just default to 1.
    picker->setEffectivePageCount(remoteEngine_ != nullptr ? remoteEngine_->getEffectivePageCount(selectedProfileId_)
                                                           : 1);
    juce::Component::SafePointer<MidiRemotePanelComponent> safePanel(this);
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(picker), anchor.getScreenBounds(), nullptr);
    if (auto* content = dynamic_cast<ActionPickerComponent*>(box.getChildComponent(0))) {
        juce::Component::SafePointer<juce::CallOutBox> safeBox(&box);
        content->onChosen = [safePanel, safeBox](const juce::String& actionId) {
            if (safePanel != nullptr)
                safePanel->assignSelectedControlToAction(actionId);
            if (safeBox != nullptr)
                safeBox->dismiss();
        };
        // FRO236 (docs/control/midi-remote.md#continuous-targets): the picker's "Continuous" group.
        content->onContinuousChosen = [safePanel, safeBox](synth::ContinuousTargetKind kind) {
            if (safePanel != nullptr)
                safePanel->assignSelectedControlToContinuous(kind);
            if (safeBox != nullptr)
                safeBox->dismiss();
        };
        // FRO142: the picker's "Pages" group.
        content->onPageChosen = [safePanel, safeBox](synth::PageCommand command, int page) {
            if (safePanel != nullptr)
                safePanel->assignSelectedControlToPage(command, page);
            if (safeBox != nullptr)
                safeBox->dismiss();
        };
    }
}

bool MidiRemotePanelComponent::assignSelectedControlToAction(const juce::String& actionId) {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty() || selectedControlId_.isEmpty())
        return false;
    const auto status = learnController_->assignControl(selectedProfileId_, selectedControlId_,
                                                        synth::midi::PickTarget::action(actionId));
    if (status != synth::midi::AssignStatus::assigned)
        return false;
    refreshSurfaceForSelectedProfile();
    controllerSurface_.setSelectedControlId(selectedControlId_);
    refreshInspectorForSelection();
    return true;
}

// FRO236 (docs/control/midi-remote.md#continuous-targets): mirrors assignSelectedControlToAction
// above.
bool MidiRemotePanelComponent::assignSelectedControlToContinuous(synth::ContinuousTargetKind kind) {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty() || selectedControlId_.isEmpty())
        return false;
    const auto status = learnController_->assignControl(selectedProfileId_, selectedControlId_,
                                                        synth::midi::PickTarget::continuousTarget(kind));
    if (status != synth::midi::AssignStatus::assigned)
        return false;
    refreshSurfaceForSelectedProfile();
    controllerSurface_.setSelectedControlId(selectedControlId_);
    refreshInspectorForSelection();
    return true;
}

// FRO142 (docs/control/midi-remote.md#pages): mirrors assignSelectedControlToContinuous above.
bool MidiRemotePanelComponent::assignSelectedControlToPage(synth::PageCommand command, int page) {
    if (learnController_ == nullptr || selectedProfileId_.isEmpty() || selectedControlId_.isEmpty())
        return false;
    const auto status = learnController_->assignControl(selectedProfileId_, selectedControlId_,
                                                        synth::midi::PickTarget::pageTarget(command, page));
    if (status != synth::midi::AssignStatus::assigned)
        return false;
    refreshSurfaceForSelectedProfile();
    controllerSurface_.setSelectedControlId(selectedControlId_);
    refreshInspectorForSelection();
    return true;
}

} // namespace synth::ui
