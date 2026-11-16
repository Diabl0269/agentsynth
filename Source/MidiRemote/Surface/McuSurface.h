#pragma once

// Decodes one Mackie Control port into SurfaceEvents
// (docs/control/midi-remote-mcu-hui.md#2-protocolsurface-midi-path-half).

#include "MidiRemote/Surface/ProtocolSurface.h"

namespace synth::midi {

class McuSurface final : public ProtocolSurface {
public:
    void decode(const juce::MidiMessage& message) noexcept override;

    /** True for an MCU device-ready sysex (F0 00 00 66 14 01 ...). */
    static bool isDeviceReady(const SurfaceEvent& event) noexcept;
};

} // namespace synth::midi
