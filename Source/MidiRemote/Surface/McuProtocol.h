#pragma once

// Mackie Control Universal wire numbers, exactly as docs/control/midi-remote-mcu-hui.md#mackie-control-universal-mcu
// lists them. Nothing here is invented: an unverified number from that doc is not used in phase 1.

#include <cstdint>

namespace synth::midi::mcu {

inline constexpr int kStripsPerUnit = 8;
inline constexpr int kMasterFaderIndex = 8; // pitch bend channel 9, touch note 112
inline constexpr int kFaderMax = 16383;     // 14-bit pitch bend

inline constexpr int kTouchFirstNote = 104; // 104-111 strips, 112 master
inline constexpr int kTouchLastNote = 112;
inline constexpr int kTouchedAbove = 64; // velocity > 64 = touched

inline constexpr int kRecFirstNote = 0;
inline constexpr int kSoloFirstNote = 8;
inline constexpr int kMuteFirstNote = 16;
inline constexpr int kSelectFirstNote = 24;
inline constexpr int kVPotPushFirstNote = 32;

inline constexpr int kBankLeftNote = 46;
inline constexpr int kBankRightNote = 47;
inline constexpr int kChannelLeftNote = 48;
inline constexpr int kChannelRightNote = 49;

inline constexpr int kVPotFirstCc = 16; // 16-23, sign-magnitude relative
inline constexpr int kJogCc = 60;

inline constexpr std::uint8_t kLedOn = 0x7F;
inline constexpr std::uint8_t kLedOff = 0x00;

// Sysex: F0 00 00 66 14 <type> ... F7. Type 01 = device ready.
inline constexpr std::uint8_t kSysexHeader[] = {0xF0, 0x00, 0x00, 0x66, 0x14};
inline constexpr int kSysexHeaderBytes = 5;
inline constexpr std::uint8_t kDeviceReadyType = 0x01;

} // namespace synth::midi::mcu
