#include "MidiRemote/ControllerHandshake.h"

#include <algorithm>
#include <functional>
#include <iterator>

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

// Case-sensitive whole-word trailing swap: "LCXL3 1 DAW Out" + ("Out","In") -> "LCXL3 1 DAW In".
// Never matches a trailing SUBSTRING that isn't its own word (e.g. "Throughout") -- the character
// right before `fromWord` must be whitespace, or `fromWord` must be the whole string.
juce::String swapTrailingWord(const juce::String& name, const juce::String& fromWord, const juce::String& toWord) {
    if (!name.endsWith(fromWord))
        return name;
    const int cutPoint = name.length() - fromWord.length();
    if (cutPoint > 0 && !juce::CharacterFunctions::isWhitespace(name[cutPoint - 1]))
        return name;
    return name.substring(0, cutPoint) + toWord;
}

std::optional<ControllerProfile::Input>
findByPredicate(const std::vector<ControllerProfile::Input>& outputs,
                const std::function<bool(const ControllerProfile::Input&)>& pred) {
    const auto it = std::find_if(outputs.begin(), outputs.end(), pred);
    return it == outputs.end() ? std::nullopt : std::optional<ControllerProfile::Input>(*it);
}

} // namespace

// Applies a handshake's `port` hint to `deviceName` by replacing the token right before its
// trailing "In"/"Out" direction word -- e.g. "LCXL3 1 MIDI Out" + "DAW" -> "LCXL3 1 DAW Out".
// Returns `deviceName` unchanged when `portHint` is empty, `deviceName` has fewer than two
// space-separated tokens, or its last token isn't exactly "In" or "Out" (nothing to retarget). Free
// function (not a coordinator member) so resolveHandshakeOutput() and the panel's status-line
// wording (describeHandshakeIssue()) use the exact same transform.
juce::String applyHandshakePortHint(const juce::String& deviceName, const juce::String& portHint) {
    if (portHint.isEmpty())
        return deviceName;
    juce::StringArray tokens;
    tokens.addTokens(deviceName, " ", "");
    if (tokens.size() < 2)
        return deviceName;
    const auto& lastToken = tokens[tokens.size() - 1];
    if (lastToken != "In" && lastToken != "Out")
        return deviceName; // no recognised trailing direction word -- nothing to retarget
    tokens.set(tokens.size() - 2, portHint);
    return tokens.joinIntoString(" ");
}

// Resolves the MIDI OUTPUT a profile's handshake should be sent to, given `input` (the profile's own
// `input` device), an optional `portHint` (ControllerProfile::Handshake::port), and every output the
// app can currently see. 2026-09-28 hardware finding (docs/control/midi-remote-device-handshake.md
// #device-handshake): CoreMIDI gives an input/output pair for the SAME physical port asymmetric
// names, so matching on the input's own identifier/name only worked by accident. Order below.
std::optional<ControllerProfile::Input>
resolveHandshakeOutput(const ControllerProfile::Input& input, const juce::String& portHint,
                       const std::vector<ControllerProfile::Input>& availableOutputs) {
    // 1. Exact identifier match -- never touched by `portHint` (an identifier carries no port
    // wording of its own; it's opaque).
    if (input.identifier.isNotEmpty()) {
        if (auto found = findByPredicate(
                availableOutputs, [&](const ControllerProfile::Input& o) { return o.identifier == input.identifier; }))
            return found;
    }

    // `portHint` is applied to the candidate name ONCE, before both of the name-based checks below
    // -- so a profile still pointed at the wrong sibling port (e.g. "LCXL3 1 MIDI Out" with hint
    // "DAW") is retargeted at "LCXL3 1 DAW Out" before either check runs.
    const juce::String candidateName = applyHandshakePortHint(input.name, portHint);
    if (candidateName.isEmpty())
        return std::nullopt;

    // 2. Exact name match (covers a simple device whose in/out ports share one name).
    if (auto found = findByPredicate(availableOutputs,
                                     [&](const ControllerProfile::Input& o) { return o.name == candidateName; }))
        return found;

    // 3. The candidate name's trailing "Out" swapped to "In" (the asymmetric-port case FRO339
    // shipped for).
    const juce::String swapped = swapTrailingWord(candidateName, "Out", "In");
    if (swapped != candidateName) {
        if (auto found =
                findByPredicate(availableOutputs, [&](const ControllerProfile::Input& o) { return o.name == swapped; }))
            return found;
    }

    return std::nullopt;
}

// Words the ONE status line MidiRemotePanelComponent shows for a profile whose handshake has a
// problem, or "" when it doesn't. Pure -- ControllerHandshakeCoordinator::reconcile() is what
// actually calls resolveHandshakeOutput() and decides `resolved`; this only formats the message.
juce::String describeHandshakeIssue(const ControllerProfile::Input& input, const juce::String& portHint,
                                    bool resolved) {
    // (1) Wrong sibling port picked as the INPUT -- the more actionable problem, checked first: even
    // when `resolved` is true (the hint swap found an output to send the handshake to), every OTHER
    // message on the right port (Play/Record, encoders, ...) never reaches the app, since the input
    // the app actually listens on is still the wrong one.
    if (portHint.isNotEmpty() && !input.name.contains(portHint)) {
        return "This template needs the device's " + portHint + " port: pick \"" +
               applyHandshakePortHint(input.name, portHint) + "\" as the input.";
    }
    // (2) No output resolved at all -- the handshake (and, unless it's the sole message affected,
    // controller feedback too) has nowhere to go.
    if (!resolved)
        return "This controller's handshake couldn't find a matching MIDI output for \"" + input.name + "\".";
    return {};
}

// Sends `handshake.openMessage` for every profile with a non-empty handshake whose input device is
// in `openSourceKeys` (AudioEngine::getOpenMidiInputIdentifiers()) and isn't already tracked as open
// -- to resolveHandshakeOutput(profile.input, profile.handshake.port, availableOutputs)'s result, NOT
// to profile.input itself (see RemoteModel.h's own Handshake comment on why that used to be wrong).
// A profile whose output can't be resolved is skipped, not asserted -- see getHandshakeIssue().
// Sends `handshake.closeMessage` for every previously-open profile that no longer qualifies --
// removed, handshake cleared, or device no longer open -- to the SAME output it opened on (never
// re-resolved: the device may already be gone), using the LAST profile state seen open. Call after
// every profile-list or open-device-set change. Idempotent: an unchanged open set sends nothing.
void ControllerHandshakeCoordinator::reconcile(const std::vector<ControllerProfile>& profiles,
                                               const std::vector<juce::String>& openSourceKeys,
                                               const std::vector<ControllerProfile::Input>& availableOutputs) {
    // Issues: recomputed every call for every profile with a handshake whose device is open,
    // independent of open_/already-sent bookkeeping below, so a still-mismatched port keeps
    // reporting even once the open bytes have already latched. Logged only on the transition into a
    // NEW (or newly-different) issue -- reconcile() runs on every profile edit, not just device
    // changes, so logging unconditionally would spam.
    for (const auto& profile : profiles) {
        if (profile.handshake.isEmpty() || !deviceIsOpen(profile, openSourceKeys)) {
            issues_.erase(profile.id);
            continue;
        }
        const auto resolved = resolveHandshakeOutput(profile.input, profile.handshake.port, availableOutputs);
        const juce::String issue = describeHandshakeIssue(profile.input, profile.handshake.port, resolved.has_value());
        const auto existing = issues_.find(profile.id);
        const bool changed = issue.isNotEmpty() && (existing == issues_.end() || existing->second != issue);
        if (issue.isEmpty())
            issues_.erase(profile.id);
        else
            issues_[profile.id] = issue;
        if (changed)
            DBG("ControllerHandshakeCoordinator: " + profile.id + ": " + issue);
    }
    for (auto it = issues_.begin(); it != issues_.end();) {
        const bool stillPresent = std::any_of(profiles.begin(), profiles.end(),
                                              [&](const ControllerProfile& p) { return p.id == it->first; });
        it = stillPresent ? std::next(it) : issues_.erase(it);
    }

    // Open: every profile with a handshake whose device is open and not already tracked.
    for (const auto& profile : profiles) {
        if (profile.handshake.isEmpty() || !deviceIsOpen(profile, openSourceKeys))
            continue;
        if (open_.find(profile.id) != open_.end())
            continue; // already sent -- the device latches, so re-sending on every reconcile is pointless
        const auto resolved = resolveHandshakeOutput(profile.input, profile.handshake.port, availableOutputs);
        if (!resolved)
            continue; // no matching output -- already reported via issues_ above
        sendHandshakeMessage(sink_, *resolved, profile.handshake.openMessage);
        open_.emplace(profile.id, OpenEntry{profile.handshake, *resolved});
    }

    // Close: anything tracked as open whose profile is gone, lost its handshake, or whose device
    // is no longer open -- a removed profile is no longer in `profiles`, so the close is sent from
    // the LAST state this coordinator saw for it, not a fresh lookup, to the SAME output `open` was
    // sent to (never re-resolved -- the device may already be gone).
    for (auto it = open_.begin(); it != open_.end();) {
        const auto found = std::find_if(profiles.begin(), profiles.end(),
                                        [&](const ControllerProfile& p) { return p.id == it->first; });
        const bool stillWantsHandshake =
            found != profiles.end() && !found->handshake.isEmpty() && deviceIsOpen(*found, openSourceKeys);
        if (stillWantsHandshake) {
            ++it;
            continue;
        }
        sendHandshakeMessage(sink_, it->second.output, it->second.handshake.closeMessage);
        it = open_.erase(it);
    }
}

void ControllerHandshakeCoordinator::shutdownAll() {
    for (const auto& [profileId, entry] : open_)
        sendHandshakeMessage(sink_, entry.output, entry.handshake.closeMessage);
    open_.clear();
}

juce::String ControllerHandshakeCoordinator::getHandshakeIssue(const juce::String& profileId) const {
    const auto it = issues_.find(profileId);
    return it == issues_.end() ? juce::String() : it->second;
}

} // namespace synth::midi
