#include "MidiRemote/ControllerHandshake.h"

#include <algorithm>

namespace synth::midi {

namespace {

// Fire-and-forget, mirroring RemoteFeedbackSink::sendFeedback's own contract: a device that can't
// be opened is the sink implementation's problem to remember and log once, not this call's.
void sendHandshakeMessage(RemoteFeedbackSink& sink, const ControllerProfile::Input& output,
                          const std::vector<std::uint8_t>& bytes) {
    if (bytes.empty())
        return;
    sink.sendFeedback(output, juce::MidiMessage(bytes.data(), static_cast<int>(bytes.size())));
}

bool deviceIsOpen(const ControllerProfile& profile, const std::vector<juce::String>& openSourceKeys) {
    return std::find(openSourceKeys.begin(), openSourceKeys.end(), profile.input.identifier) != openSourceKeys.end();
}

} // namespace

void ControllerHandshakeCoordinator::reconcile(const std::vector<ControllerProfile>& profiles,
                                               const std::vector<juce::String>& openSourceKeys) {
    // Open: every profile with a handshake whose device is open and not already tracked.
    for (const auto& profile : profiles) {
        if (profile.handshake.isEmpty() || !deviceIsOpen(profile, openSourceKeys))
            continue;
        if (open_.find(profile.id) != open_.end())
            continue; // already sent -- the device latches, so re-sending on every reconcile is pointless
        sendHandshakeMessage(sink_, profile.input, profile.handshake.openMessage);
        open_.emplace(profile.id, OpenEntry{profile.handshake, profile.input});
    }

    // Close: anything tracked as open whose profile is gone, lost its handshake, or whose device
    // is no longer open -- a removed profile is no longer in `profiles`, so the close is sent from
    // the LAST state this coordinator saw for it, not a fresh lookup.
    for (auto it = open_.begin(); it != open_.end();) {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
                                        [&](const ControllerProfile& p) { return p.id == it->first; });
        const bool stillWantsHandshake =
            found != profiles.end() && !found->handshake.isEmpty() && deviceIsOpen(*found, openSourceKeys);
        if (stillWantsHandshake) {
            ++it;
            continue;
        }
        sendHandshakeMessage(sink_, it->second.input, it->second.handshake.closeMessage);
        it = open_.erase(it);
    }
}

void ControllerHandshakeCoordinator::shutdownAll() {
    for (const auto& [profileId, entry] : open_)
        sendHandshakeMessage(sink_, entry.input, entry.handshake.closeMessage);
    open_.clear();
}

} // namespace synth::midi
