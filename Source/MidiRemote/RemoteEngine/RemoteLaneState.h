#pragma once

// The MIDI path's per-source memory for the two encodings whose value spans two messages
// (FRO140, docs/control/midi-remote.md#14-bit-and-nrpn-encodings): the last MSB/LSB halves of a
// CC n / CC n+32 pair, and each channel's armed NRPN address with its data-entry halves.
//
// THREAD CONTRACT. Every member is read and written ONLY by the lane's single producer (the one
// device thread, or the audio thread in hosted mode) inside RemoteEngine::handleMessage -- the
// same one-producer-per-lane rule that lets SourceLane's rings be single-producer. The message
// thread never touches it, so it needs neither a lock nor atomics; keep it that way (the drain
// reads events, not this). Fixed-size and trivially constructible: no allocation on the MIDI path.

#include <cstdint>

namespace synth::midi {

/** The two 7-bit halves of one 14-bit value, each remembered until replaced. */
struct PairHalves {
    std::uint8_t msb = 0;
    std::uint8_t lsb = 0;
    bool haveMsb = false;
    bool haveLsb = false;

    int value14() const noexcept { return (static_cast<int>(msb) << 7) | static_cast<int>(lsb); }
};

/** One MIDI channel's NRPN receive state (CC 99/98 arm an address, CC 6/38 carry its value). */
struct NrpnChannelState {
    std::uint8_t addressMsb = 0;
    bool haveAddressMsb = false;
    bool active = false; // both address CCs seen and no RPN select since
    int address = 0;
    PairHalves data;
};

struct LaneState {
    static constexpr int kChannels = 16;
    static constexpr int kPairedControllers = 32; // MSB CC 0..31; LSB is n + 32

    PairHalves pairs[kChannels][kPairedControllers];
    NrpnChannelState nrpn[kChannels];
};

} // namespace synth::midi
