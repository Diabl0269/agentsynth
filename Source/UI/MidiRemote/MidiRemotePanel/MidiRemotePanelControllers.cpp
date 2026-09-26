// Concern: FRO134 -- creating and importing controllers from the panel: "+ Add controller", the
// Templates menu, and Import/Export (docs/control/midi-remote-ui.md#add-controller,
// #templates-and-importexport).
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/ControllerTemplates.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemotePanelComponent.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::ui {

// ---- Add controller ---------------------------------------------------------------------------------

void MidiRemotePanelComponent::showAddControllerPopover(juce::Component& anchor) {
    if (learnController_ == nullptr)
        return;

    std::vector<AddControllerPopover::DeviceRow> devices;
    for (const auto& info : juce::MidiInput::getAvailableDevices()) {
        AddControllerPopover::DeviceRow row;
        row.identifier = info.identifier;
        row.name = info.name;
        for (const auto& profile : learnController_->getProfiles())
            if (profile.input.identifier == info.identifier)
                row.existingProfileName = profile.name;
        devices.push_back(std::move(row));
    }

    auto popover = std::make_unique<AddControllerPopover>(std::move(devices), synth::midi::listControllerTemplates());
    juce::Component::SafePointer<MidiRemotePanelComponent> safePanel(this);
    popover->onConfirmed = [safePanel](const AddControllerPopover::Choice& choice) {
        if (safePanel != nullptr)
            safePanel->createControllerFromChoice(choice);
    };
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(popover), anchor.getScreenBounds(), nullptr);
    if (auto* content = dynamic_cast<AddControllerPopover*>(box.getChildComponent(0))) {
        juce::Component::SafePointer<juce::CallOutBox> safeBox(&box);
        auto confirmed = content->onConfirmed;
        content->onConfirmed = [safeBox, confirmed](const AddControllerPopover::Choice& choice) {
            if (confirmed)
                confirmed(choice);
            if (safeBox != nullptr)
                safeBox->dismiss();
        };
        content->onCancelled = [safeBox] {
            if (safeBox != nullptr)
                safeBox->dismiss();
        };
    }
}

// The device is opened by NAME (AudioEngine::ensureMidiDeviceOpen's key) but the profile stores its
// IDENTIFIER (the engine's source key), and RemoteEngine ignores a source it has not been told
// about -- so the sources are republished right after the open, or Detect would hear nothing.
juce::String MidiRemotePanelComponent::createControllerFromChoice(const AddControllerPopover::Choice& choice) {
    if (learnController_ == nullptr || audioEngine_ == nullptr || choice.deviceIdentifier.isEmpty())
        return {};

    synth::ControllerProfile profile;
    profile.id = juce::Uuid().toDashedString();
    profile.name = choice.profileName;
    profile.input.identifier = choice.deviceIdentifier;
    profile.input.name = choice.deviceName;

    if (choice.startWith == AddControllerPopover::StartWith::templateLayout) {
        synth::ControllerProfile tmpl;
        if (synth::midi::loadControllerTemplate(choice.templateId, tmpl))
            synth::midi::applyControllerTemplate(profile, tmpl);
    }

    if (!learnController_->addProfile(profile))
        return {};
    audioEngine_->ensureMidiDeviceOpen(choice.deviceName);
    learnController_->refreshSources();

    rebuildFromProfiles();
    selectProfile(profile.id);
    setDetectActive(choice.startWith == AddControllerPopover::StartWith::detect);
    return profile.id;
}

// ---- Templates ------------------------------------------------------------------------------------------

int MidiRemotePanelComponent::applyTemplateToSelectedProfile(const juce::String& templateId) {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr || learnController_ == nullptr)
        return -1;
    synth::ControllerProfile tmpl;
    if (!synth::midi::loadControllerTemplate(templateId, tmpl))
        return -1;

    auto updated = *profile;
    const auto result = synth::midi::applyControllerTemplate(updated, tmpl);
    if (result.added > 0) {
        learnController_->updateProfile(updated);
        refreshSurfaceForSelectedProfile();
        controllerSurface_.setSelectedControlId(selectedControlId_);
    }
    return result.added;
}

void MidiRemotePanelComponent::showTemplatesMenu(juce::Component& anchor) {
    juce::PopupMenu menu;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    const auto templates = synth::midi::listControllerTemplates();
    for (const auto& group : synth::midi::groupControllerTemplatesByVendor(templates)) {
        menu.addSectionHeader(group.vendor.isEmpty() ? juce::String("Generic") : group.vendor);
        for (const auto& info : group.templates) {
            menu.addItem(info.name, [safeThis, id = info.id] {
                if (safeThis != nullptr)
                    safeThis->applyTemplateToSelectedProfile(id);
            });
        }
    }
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

// ---- Import / export ------------------------------------------------------------------------------------

void MidiRemotePanelComponent::showMoreMenu(juce::Component& anchor) {
    juce::PopupMenu menu;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    menu.addItem("Import controller...", [safeThis] {
        if (safeThis != nullptr)
            safeThis->showImportChooser();
    });
    menu.addItem("Export controller...", findSelectedProfile() != nullptr, false, [safeThis] {
        if (safeThis != nullptr)
            safeThis->handleExportRequested(safeThis->selectedProfileId_);
    });
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

void MidiRemotePanelComponent::showImportChooser() {
    auto chooser = std::make_shared<juce::FileChooser>(
        "Import controller...", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    const auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    chooser->launchAsync(flags, [safeThis, chooser](const juce::FileChooser& fc) {
        const auto file = fc.getResult();
        if (safeThis != nullptr && file != juce::File())
            safeThis->importControllerFile(file);
    });
}

void MidiRemotePanelComponent::importControllerFile(const juce::File& file) {
    switch (importControllerFileNow(file, false)) {
    case ImportOutcome::conflict:
        showPrompt("Import controller",
                   "A controller with this ID is already set up on this machine. Replace it with the imported "
                   "one? Its assignments in your projects stay linked.",
                   true, [this, file](bool ok) {
                       if (ok)
                           importControllerFileNow(file, true);
                   });
        break;
    case ImportOutcome::invalid:
        showPrompt("Import controller", "That file isn't a valid controller profile.", false, {});
        break;
    default:
        break;
    }
}

MidiRemotePanelComponent::ImportOutcome MidiRemotePanelComponent::importControllerFileNow(const juce::File& file,
                                                                                          bool replaceExisting) {
    if (learnController_ == nullptr)
        return ImportOutcome::invalid;
    const auto result = learnController_->importProfile(file, replaceExisting);
    switch (result.status) {
    case synth::midi::MidiLearnController::ImportStatus::conflict:
        return ImportOutcome::conflict;
    case synth::midi::MidiLearnController::ImportStatus::invalid:
        return ImportOutcome::invalid;
    default:
        break;
    }
    rebuildFromProfiles();
    selectProfile(result.profile.id);
    return result.status == synth::midi::MidiLearnController::ImportStatus::replaced ? ImportOutcome::replaced
                                                                                     : ImportOutcome::imported;
}

} // namespace synth::ui
