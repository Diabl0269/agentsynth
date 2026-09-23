// Concern: Detect mode's event -> control mapping (docs/control/midi-remote-ui.md#detect-mode).

#include "MidiRemote/ControllerDetect.h"

#include <algorithm>
#include <juce_audio_basics/juce_audio_basics.h> // juce::MidiMessage::getMidiNoteName

namespace synth::midi {

MessageSpec specFromEvent(const RemoteEvent& event) {
    MessageSpec spec;
    spec.type = static_cast<MessageType>(event.specType);
    spec.channel = event.specChannel;
    spec.number = event.specNumber;
    return spec;
}

bool specMatchesEvent(const MessageSpec& spec, const RemoteEvent& event) {
    if (static_cast<std::uint8_t>(spec.type) != event.specType)
        return false;
    if (spec.channel != 0 && spec.channel != event.specChannel)
        return false;
    return spec.number == event.specNumber;
}

const Control* findControlForEvent(const std::vector<Control>& controls, const RemoteEvent& event) {
    for (const auto& control : controls)
        if (specMatchesEvent(control.message, event))
            return &control;
    return nullptr;
}

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

bool isDetectCandidate(const RemoteEvent& event) {
    if (event.kind == RemoteEventKind::learnCandidate || event.kind == RemoteEventKind::buttonRelease)
        return false;
    return static_cast<MessageType>(event.specType) != MessageType::programChange;
}

juce::String detectedControlName(const MessageSpec& spec) {
    switch (spec.type) {
    case MessageType::note:
        // Same note-name convention SequencerModule uses (C3 = MIDI 60).
        return juce::MidiMessage::getMidiNoteName(spec.number, true, true, 3);
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

Control makeDetectedControl(const RemoteEvent& event, const std::vector<Control>& existing) {
    Control control;
    control.message = specFromEvent(event);
    control.id = juce::Uuid().toDashedString();
    control.name = detectedControlName(control.message);
    switch (control.message.type) {
    case MessageType::note:
        control.kind = ControlKind::pad;
        break;
    case MessageType::pitchBend:
        control.kind = ControlKind::wheel;
        break;
    default:
        control.kind = ControlKind::knob;
        break;
    }
    placeAtNextFreeCell(control, existing);
    return control;
}

} // namespace synth::midi
