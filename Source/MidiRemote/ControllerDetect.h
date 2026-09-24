#pragma once

// Headless helpers for MIDI Remote's Detect mode (docs/control/midi-remote-ui.md#detect-mode):
// turning one activity event into a control on a profile. Lives in Core beside
// MidiRemoteLearnBinder.h -- pure data over RemoteEvent/Control, no UI, no store.

#include "MidiRemote/RemoteEngine/RemoteEvent.h"
#include "MidiRemote/RemoteModel.h"

#include <vector>

namespace synth::midi {

/** Columns the auto-layout fills before wrapping to the next row. */
inline constexpr int kAutoLayoutColumns = 8;

/** The message key an activity event carries (channel exactly as received, never 0 = "any"). */
MessageSpec specFromEvent(const RemoteEvent& event);

/** True when `spec` addresses `event` -- same rule the engine's lookup uses (a channel-0 spec
 *  matches every channel). */
bool specMatchesEvent(const MessageSpec& spec, const RemoteEvent& event);

/** True when `event` belongs to `control`: its own message, or -- for a paired-CC control (abs14 /
 *  abs14LsbFirst on a cc n) -- the LSB partner CC n+32 that completes it. */
bool controlMatchesEvent(const Control& control, const RemoteEvent& event);

/** True when `spec` is `control`'s own message or its paired LSB partner (same channel, number + 32). */
bool controlClaimsSpec(const Control& control, const MessageSpec& spec);

/** The control on `controls` this event belongs to, or nullptr. */
const Control* findControlForEvent(const std::vector<Control>& controls, const RemoteEvent& event);

/** Two CCs n / n+32 arriving within this many milliseconds are one 14-bit control (Detect mode). */
inline constexpr int kPairedHalvesWindowMs = 5;

/** The last CC Detect turned into a NEW control, so a partner half arriving right behind it can fold
 *  into that control instead of adding a second one. */
struct DetectedCc {
    bool valid = false;
    std::uint16_t timeMs = 0;
    int channel = 0;
    int number = 0;
    juce::String controlId;
};

/** If `event` is the other half of the 14-bit pair `previous` just created a control for (CC n then
 *  n+32 -> abs14, or n+32 then n -> abs14LsbFirst, on one channel within kPairedHalvesWindowMs),
 *  rewrites that control in `controls` into the paired control and returns true. */
bool foldIntoPairedControl(std::vector<Control>& controls, const DetectedCc& previous, const RemoteEvent& event);

/** Sets control.layout to the first {col,row} (row-major, kAutoLayoutColumns wide) no control in
 *  `existing` occupies. */
void placeAtNextFreeCell(Control& control, const std::vector<Control>& existing);

/** Whether this event may CREATE a control in Detect: not a learn candidate, not a release (the
 *  press that came first already created the control), not a program change (each program number
 *  is a different message key, so touching one button would litter the surface). */
bool isDetectCandidate(const RemoteEvent& event);

/** "CC 21", "C3" (a note), "Pitch Bend", "Channel Pressure". */
juce::String detectedControlName(const MessageSpec& spec);

/** The control Detect adds for an unknown message: kind guessed (CC -> knob, note -> pad, pitch
 *  bend -> wheel), named by detectedControlName, a fresh id, the next free cell in `existing`. */
Control makeDetectedControl(const RemoteEvent& event, const std::vector<Control>& existing);

} // namespace synth::midi
