// McuSurface: the MIDI-thread decode of the Mackie Control wire format. Every branch is a few
// integer compares and one ring push; sysex is a bounded memcpy into the event's fixed slot.
#include "McuSurface.h"

#include "MidiRemote/Surface/McuProtocol.h"

#include <cstring>

namespace synth::midi {

namespace {

// MCU relative encoders: bit 6 set = counter-clockwise, bits 0-5 = tick count.
int signMagnitude(int value) noexcept { return (value & 0x40) != 0 ? -(value & 0x3F) : (value & 0x3F); }

SurfaceEvent make(SurfaceEventKind kind, int index, int value) noexcept {
    SurfaceEvent event;
    event.kind = kind;
    event.index = static_cast<std::int16_t>(index);
    event.value = value;
    return event;
}

} // namespace

// A pitch bend on channel 1-9 is a fader (9 = master); a note is a touch (104-112) or a button
// (everything else, velocity 0 or note-off = release); CC 16-23 are V-Pots and CC 60 the jog wheel.
// Sysex that fits the fixed slot is copied whole (the controller recognises device-ready from it);
// longer sysex is counted as dropped. Anything else on an owned port is simply ignored -- the
// router has already consumed it.
void McuSurface::decode(const juce::MidiMessage& message) noexcept {
    if (message.isPitchWheel()) {
        const int channel = message.getChannel() - 1;
        if (channel >= 0 && channel <= mcu::kMasterFaderIndex)
            push(make(SurfaceEventKind::fader, channel, message.getPitchWheelValue()));
        return;
    }
    if (message.isNoteOnOrOff()) {
        const int note = message.getNoteNumber();
        const int velocity = message.isNoteOn() ? message.getVelocity() : 0;
        if (note >= mcu::kTouchFirstNote && note <= mcu::kTouchLastNote)
            push(make(SurfaceEventKind::touch, note - mcu::kTouchFirstNote, velocity > mcu::kTouchedAbove ? 1 : 0));
        else
            push(make(SurfaceEventKind::button, note, velocity > 0 ? 1 : 0));
        return;
    }
    if (message.isController()) {
        const int cc = message.getControllerNumber();
        const int value = message.getControllerValue();
        if (cc >= mcu::kVPotFirstCc && cc < mcu::kVPotFirstCc + mcu::kStripsPerUnit)
            push(make(SurfaceEventKind::encoderDelta, cc - mcu::kVPotFirstCc, signMagnitude(value)));
        else if (cc == mcu::kJogCc)
            push(make(SurfaceEventKind::jog, 0, signMagnitude(value)));
        return;
    }
    if (message.isSysEx()) {
        const int size = message.getRawDataSize();
        if (size > kSurfaceSysexBytes) {
            noteDropped();
            return;
        }
        SurfaceEvent event = make(SurfaceEventKind::sysex, 0, 0);
        event.sysexLen = static_cast<std::uint8_t>(size);
        std::memcpy(event.sysex, message.getRawData(), static_cast<std::size_t>(size));
        push(event);
    }
}

bool McuSurface::isDeviceReady(const SurfaceEvent& event) noexcept {
    if (event.kind != SurfaceEventKind::sysex || event.sysexLen <= mcu::kSysexHeaderBytes)
        return false;
    return std::memcmp(event.sysex, mcu::kSysexHeader, mcu::kSysexHeaderBytes) == 0 &&
           event.sysex[mcu::kSysexHeaderBytes] == mcu::kDeviceReadyType;
}

} // namespace synth::midi
