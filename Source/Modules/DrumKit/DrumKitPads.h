#pragma once

// The Drum Kit's pads: what each of its 32 pad slots plays and on which note, the default kit, and how the table is
// saved. Plain data, message thread only; the audio thread reads a published copy (DrumKitPadVoice.h).

#include <array>
#include <juce_core/juce_core.h>

namespace synth::drums {

/** What a pad plays. Kit: the loaded recorded kit's region for the pad's sound, or the built-in drum without one.
    BuiltIn: the synthesized drum. File: an audio file. Instrument: a module elsewhere in the patch (stored only). */
enum class PadKind { Kit, BuiltIn, File, Instrument };

inline constexpr int kMaxPads = 32; // always 32 slots: the count only decides how many the card shows
inline constexpr int kMinPads = 4;
inline constexpr int kPadStep = 4;
inline constexpr int kDefaultPadCount = 8;
inline constexpr int kNoSound = -1;

struct Pad {
    PadKind kind = PadKind::BuiltIn;
    /** The General MIDI note whose built-in drum this pad plays (a pedal hat is 44, a ride 51, each tom its own
        pitch), or kNoSound. Also the drum's group for the old group parameters and the recorded kit's region. */
    int sound = kNoSound;
    int note = 60; // the MIDI note that plays the pad
    juce::String name;
    juce::String file;           // PadKind::File: absolute path
    juce::String instrumentUuid; // PadKind::Instrument: the module it plays
    int instrumentPitch = 60;
    bool shown = true;

    bool operator==(const Pad& o) const {
        return kind == o.kind && sound == o.sound && note == o.note && name == o.name && file == o.file &&
               instrumentUuid == o.instrumentUuid && instrumentPitch == o.instrumentPitch && shown == o.shown;
    }
    bool operator!=(const Pad& o) const { return !(*this == o); }
};

struct PadTable {
    std::array<Pad, kMaxPads> pads;
    int count = kDefaultPadCount;
};

/** Pad `index` (0-based) as a new kit has it: 1-20 play the General MIDI kit, 21-32 are free notes with no sound. */
Pad defaultPad(int index);
PadTable defaultPadTable();

/** 4..32 in steps of 4 (rounded up). */
int clampPadCount(int count) noexcept;

/** The built-in sound a never-used free pad gets when the count first shows it (kNoSound for any other pad). */
int revealSoundFor(int index) noexcept;

/** "D1" for 38 (middle C is C3). */
juce::String noteName(int note);

/** True for the closed and pedal hats, which choke every open hat. */
inline bool chokesOpenHat(int sound) noexcept { return sound == 42 || sound == 44; }
inline bool isOpenHat(int sound) noexcept { return sound == 46; }

/** Pad table <-> the node state keys "padCount", "pads" (an array of 32 objects) and "padFiles" (32 paths, empty
    for a pad with no file; a flat list so the project bundle can rewrite them). */
void writePadState(const PadTable& table, juce::DynamicObject& state);
/** False (leaving `table` alone) when `state` holds no pad table: a project saved before pads existed. Every value
    is clamped. */
bool readPadState(const juce::DynamicObject& state, PadTable& table);

} // namespace synth::drums
