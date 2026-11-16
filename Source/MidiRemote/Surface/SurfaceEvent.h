#pragma once

// The POD a protocol surface's MIDI-thread decode hands the message thread
// (docs/control/midi-remote-mcu-hui.md#2-protocolsurface-midi-path-half). Trivially copyable, so it rides an SpscRing.

#include <cstdint>

namespace synth::midi {

/** Sysex longer than this (an LCD echo, a firmware dump) is dropped on the MIDI path. */
inline constexpr int kSurfaceSysexBytes = 20;

enum class SurfaceEventKind : std::uint8_t { fader, touch, encoderDelta, button, jog, sysex };

struct SurfaceEvent {
    SurfaceEventKind kind = SurfaceEventKind::button;
    std::uint8_t unit = 0;     // which unit of a multi-port surface; 0 for a single MCU
    std::int16_t index = 0;    // fader/touch: strip 0-7, 8 = master; button: the note number; encoder: 0-7
    std::int32_t value = 0;    // fader: 14-bit 0-16383; touch/button: 1 down, 0 up; encoder/jog: signed ticks
    std::uint8_t sysexLen = 0; // sysex only: bytes used in `sysex`, F0 and F7 included
    std::uint8_t sysex[kSurfaceSysexBytes] = {};
};

} // namespace synth::midi
