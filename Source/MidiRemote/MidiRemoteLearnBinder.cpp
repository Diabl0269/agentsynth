#include "MidiRemote/MidiRemoteLearnBinder.h"

#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h> // juce::MidiMessage::getMidiNoteName

namespace synth::midi {

namespace {

// docs/control/midi-remote.md#learn-what-does-the-first-message-mean: name "CC 21" / "Note C3".
// getMidiNoteName(n, useSharps=true, includeOctave=true, octaveForMiddleC=3) matches the naming
// convention SequencerModule/PolySequencerModule already use for note numbers in this codebase.
juce::String nameForSpec(const MessageSpec& spec) {
    switch (spec.type) {
    case MessageType::note:
        return "Note " + juce::MidiMessage::getMidiNoteName(spec.number, true, true, 3);
    case MessageType::pitchBend:
        return "Pitch Bend";
    case MessageType::channelPressure:
        return "Channel Pressure";
    case MessageType::programChange:
        return "Program " + juce::String(spec.number);
    case MessageType::cc:
    default:
        return "CC " + juce::String(spec.number);
    }
}

// "kind guessed: CC -> knob, note -> button" (same doc section). Everything else defaults to
// knob -- pitch bend/channel pressure/program change are continuous-ish oddities a learn rarely
// lands on, and the panel lets the user retype it either way.
ControlKind kindForSpec(const MessageSpec& spec) {
    return spec.type == MessageType::note ? ControlKind::button : ControlKind::knob;
}

constexpr int kAutoLayoutColumns = 8;

// The first {col, row} (row-major, kAutoLayoutColumns wide) not already occupied by a control on
// this profile -- "layout: next free grid cell" (same doc section). Deliberately simple: the user
// drags controls around in the panel afterwards (docs/control/midi-remote.md#the-surface-is-detected-not-drawn).
void placeAtNextFreeCell(Control& control, const std::vector<Control>& existing) {
    for (int row = 0;; ++row) {
        for (int col = 0; col < kAutoLayoutColumns; ++col) {
            const bool taken = std::any_of(existing.begin(), existing.end(), [&](const Control& c) {
                return c.layout.col == col && c.layout.row == row;
            });
            if (!taken) {
                control.layout.col = col;
                control.layout.row = row;
                return;
            }
        }
    }
}

} // namespace

LearnBindOutcome bindLearnResult(const LearnResult& result, const juce::String& deviceName,
                                 const std::vector<ControllerProfile>& profiles) {
    LearnBindOutcome outcome;

    const auto existingProfile = std::find_if(profiles.begin(), profiles.end(), [&](const ControllerProfile& p) {
        return p.input.identifier == result.sourceKey;
    });

    ControllerProfile profile;
    if (existingProfile != profiles.end()) {
        profile = *existingProfile;
    } else {
        outcome.profileIsNew = true;
        profile.id = juce::Uuid().toDashedString();
        profile.name = deviceName;
        profile.input.identifier = result.sourceKey;
        profile.input.name = deviceName;
    }

    const auto existingControl = std::find_if(profile.controls.begin(), profile.controls.end(),
                                              [&](const Control& c) { return c.message == result.spec; });

    Control control;
    if (existingControl != profile.controls.end()) {
        // Same message key already on this profile: keep its identity/name/layout, but the
        // encoding/buttonMode just observed is the freshest evidence of how the hardware behaves.
        control = *existingControl;
        control.encoding = result.encoding;
        control.buttonMode = result.buttonMode;
        *existingControl = control;
    } else {
        outcome.controlIsNew = true;
        control.id = juce::Uuid().toDashedString();
        control.name = nameForSpec(result.spec);
        control.kind = kindForSpec(result.spec);
        control.message = result.spec;
        control.encoding = result.encoding;
        control.buttonMode = result.buttonMode;
        placeAtNextFreeCell(control, profile.controls);
        profile.controls.push_back(control);
    }

    outcome.profile = profile;

    Assignment assignment;
    assignment.id = juce::Uuid().toDashedString();
    assignment.control.profileId = profile.id;
    assignment.control.controlId = control.id;
    assignment.spec = result.spec;
    assignment.specEncoding = result.encoding;
    assignment.specButtonMode = result.buttonMode;
    assignment.specControlName = control.name;
    assignment.target = result.target;
    assignment.takeover = Takeover::useDefault;
    assignment.range.min = 0.0;
    assignment.range.max = 1.0;
    assignment.enabled = true;
    outcome.assignment = assignment;

    return outcome;
}

} // namespace synth::midi
