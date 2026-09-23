// Concern: FRO136 -- the plugin build's (HostMode::Hosted) controller model in the panel: Host MIDI
// is the one live controller, always listed; profiles for real devices are listed as standalone-only
// and inert (docs/control/midi-remote-ui.md#plugin-build).
#include "AudioEngine/AudioEngine.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/RemoteEngine/RemoteMessageSink.h"
#include "MidiRemotePanelComponent.h"

#include <algorithm>

namespace synth::ui {

bool MidiRemotePanelComponent::hostMidiProfileExists() const {
    if (learnController_ == nullptr)
        return false;
    const auto& profiles = learnController_->getProfiles();
    return std::any_of(profiles.begin(), profiles.end(),
                       [](const auto& p) { return p.input.identifier == synth::midi::hostSourceKey(); });
}

// Empty, like any profile a Learn or Detect is about to fill; persisted like one too, so it is there
// the next time this plugin instance (or any other) opens.
void MidiRemotePanelComponent::createHostMidiProfile() {
    if (learnController_ == nullptr)
        return;
    synth::ControllerProfile profile;
    profile.id = kHostMidiProfileId;
    profile.name = "Host MIDI";
    profile.input.identifier = synth::midi::hostSourceKey();
    profile.input.name = "Host MIDI";
    learnController_->addProfile(profile);
}

bool MidiRemotePanelComponent::isSelectedProfileUsable() const {
    const auto* profile = findSelectedProfile();
    if (profile == nullptr)
        return false;
    return audioEngine_ == nullptr || !audioEngine_->isHosted() || isProfilePresent(*profile);
}

} // namespace synth::ui
