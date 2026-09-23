// Concern: assignment construction and orphan-controller repair (see MidiRemoteMapping.h).

#include "MidiRemote/MidiRemoteMapping.h"

#include "MidiRemote/ControllerDetect.h"

#include <algorithm>

namespace synth::midi {

namespace {

ControlKind guessKind(const MessageSpec& spec) {
    switch (spec.type) {
    case MessageType::note:
        return ControlKind::pad;
    case MessageType::pitchBend:
        return ControlKind::wheel;
    default:
        return ControlKind::knob;
    }
}

void copyControlOntoAssignment(Assignment& a, const ControllerProfile& profile, const Control& control) {
    a.control.profileId = profile.id;
    a.control.controlId = control.id;
    a.spec = control.message;
    a.specControlName = control.name;
    a.specEncoding = control.encoding;
    a.specButtonMode = control.buttonMode;
}

} // namespace

Assignment makeAssignmentForControl(const ControllerProfile& profile, const Control& control, const Target& target) {
    Assignment a;
    a.id = juce::Uuid().toDashedString();
    copyControlOntoAssignment(a, profile, control);
    a.target = target;
    a.takeover = Takeover::useDefault;
    a.range.min = 0.0;
    a.range.max = 1.0;
    a.enabled = true;
    return a;
}

RelinkResult relinkAssignments(std::vector<Assignment>& assignments, const juce::String& orphanProfileId,
                               const ControllerProfile& target) {
    RelinkResult result;
    for (auto& a : assignments) {
        if (a.control.profileId != orphanProfileId)
            continue;
        const auto match = std::find_if(target.controls.begin(), target.controls.end(),
                                        [&](const Control& c) { return c.message == a.spec; });
        if (match == target.controls.end()) {
            ++result.unmatched;
            continue;
        }
        copyControlOntoAssignment(a, target, *match);
        ++result.matched;
    }
    return result;
}

ControllerProfile recreateProfileFromAssignments(std::vector<Assignment>& assignments,
                                                 const juce::String& orphanProfileId, const juce::String& name,
                                                 const ControllerProfile::Input& input) {
    ControllerProfile profile;
    profile.id = juce::Uuid().toDashedString();
    profile.name = name;
    profile.input = input;

    for (auto& a : assignments) {
        if (a.control.profileId != orphanProfileId)
            continue;
        auto existing = std::find_if(profile.controls.begin(), profile.controls.end(),
                                     [&](const Control& c) { return c.message == a.spec; });
        if (existing == profile.controls.end()) {
            Control control;
            control.id = juce::Uuid().toDashedString();
            control.name = a.specControlName.isNotEmpty() ? a.specControlName : detectedControlName(a.spec);
            control.kind = guessKind(a.spec);
            control.message = a.spec;
            control.encoding = a.specEncoding;
            control.buttonMode = a.specButtonMode;
            placeAtNextFreeCell(control, profile.controls);
            profile.controls.push_back(control);
            existing = std::prev(profile.controls.end());
        }
        a.control.profileId = profile.id;
        a.control.controlId = existing->id;
    }
    return profile;
}

} // namespace synth::midi
