// Concern: FRO135 -- an orphan controller row's view and its two repairs, Re-link and Recreate
// (docs/control/midi-remote-ui.md#controllers-list-left,
// docs/control/midi-remote.md#where-does-a-mapping-live--global-or-in-the-project).
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "MidiRemotePanelComponent.h"

#include <algorithm>
#include <juce_audio_devices/juce_audio_devices.h>

namespace synth::ui {

bool MidiRemotePanelComponent::isOrphanId(const juce::String& profileId) const {
    if (doc_ == nullptr || learnController_ == nullptr || profileId.isEmpty())
        return false;
    const auto& profiles = learnController_->getProfiles();
    if (std::any_of(profiles.begin(), profiles.end(), [&](const auto& p) { return p.id == profileId; }))
        return false;
    return std::any_of(doc_->controllers.begin(), doc_->controllers.end(),
                       [&](const auto& ref) { return ref.profileId == profileId; });
}

bool MidiRemotePanelComponent::isOrphanSelected() const { return isOrphanId(selectedProfileId_); }

bool MidiRemotePanelComponent::isProfilePresent(const synth::ControllerProfile& profile) const {
    if (audioEngine_ == nullptr)
        return false;
    if (audioEngine_->isHosted())
        return profile.input.identifier == synth::midi::hostSourceKey();
    const auto open = audioEngine_->getOpenMidiInputIdentifiers();
    return std::find(open.begin(), open.end(), profile.input.identifier) != open.end();
}

std::vector<synth::ControllerProfile::Input> MidiRemotePanelComponent::getRecreateInputs() const {
    std::vector<synth::ControllerProfile::Input> inputs;
    if (learnController_ == nullptr || audioEngine_ == nullptr)
        return inputs;
    const auto& profiles = learnController_->getProfiles();
    const auto taken = [&](const juce::String& identifier) {
        return std::any_of(profiles.begin(), profiles.end(),
                           [&](const auto& p) { return p.input.identifier == identifier; });
    };
    if (audioEngine_->isHosted()) {
        if (!taken(synth::midi::hostSourceKey()))
            inputs.push_back({synth::midi::hostSourceKey(), "Host MIDI"});
        return inputs;
    }
    for (const auto& info : juce::MidiInput::getAvailableDevices())
        if (!taken(info.identifier))
            inputs.push_back({info.identifier, info.name});
    return inputs;
}

void MidiRemotePanelComponent::showRelinkMenu(juce::Component& anchor) {
    if (learnController_ == nullptr || !isOrphanSelected())
        return;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::PopupMenu menu;
    for (const auto& profile : learnController_->getProfiles()) {
        const auto label = isProfilePresent(profile) ? profile.name : profile.name + " (not connected)";
        menu.addItem(label, [safeThis, id = profile.id] {
            if (safeThis != nullptr)
                safeThis->relinkSelectedOrphanTo(id);
        });
    }
    if (menu.getNumItems() == 0)
        menu.addItem("No controllers on this machine", false, false, nullptr);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

void MidiRemotePanelComponent::showRecreateMenu(juce::Component& anchor) {
    if (!isOrphanSelected())
        return;
    juce::Component::SafePointer<MidiRemotePanelComponent> safeThis(this);
    juce::PopupMenu menu;
    for (const auto& input : getRecreateInputs())
        menu.addItem("Recreate on " + input.name, [safeThis, input] {
            if (safeThis != nullptr)
                safeThis->recreateSelectedOrphanOn(input);
        });
    if (menu.getNumItems() == 0)
        menu.addItem("Connect the controller's MIDI device first", false, false, nullptr);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&anchor));
}

synth::midi::RelinkOutcome MidiRemotePanelComponent::relinkSelectedOrphanTo(const juce::String& profileId) {
    if (learnController_ == nullptr || !isOrphanSelected())
        return {};
    const auto orphanId = selectedProfileId_;
    const auto outcome = learnController_->relinkController(orphanId, profileId);
    if (!outcome.ok)
        return outcome;

    if (outcome.unmatched == 0 && outcome.matched > 0) {
        orphanStatus_.clear();
        rebuildFromProfiles();
        selectProfile(profileId);
    } else {
        orphanStatus_ = juce::String(outcome.matched) + " of " + juce::String(outcome.matched + outcome.unmatched) +
                        " assignments linked; " + juce::String(outcome.unmatched) +
                        " stay orphaned (that controller has no control with the same message).";
        rebuildFromProfiles();
    }
    return outcome;
}

juce::String MidiRemotePanelComponent::recreateSelectedOrphanOn(const synth::ControllerProfile::Input& device) {
    if (learnController_ == nullptr || audioEngine_ == nullptr || !isOrphanSelected())
        return {};
    const auto newId = learnController_->recreateController(selectedProfileId_, device);
    if (newId.isEmpty())
        return {};
    if (!audioEngine_->isHosted()) {
        audioEngine_->ensureMidiDeviceOpen(device.name);
        learnController_->refreshSources();
    }
    orphanStatus_.clear();
    rebuildFromProfiles();
    selectProfile(newId);
    return newId;
}

} // namespace synth::ui
