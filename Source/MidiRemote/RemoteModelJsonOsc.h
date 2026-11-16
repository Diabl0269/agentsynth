#pragma once

// The osc-only JSON fields of MessageSpec and Control (docs/control/midi-remote-osc.md#9-persistence), split out of
// RemoteModelJson.cpp. Internal to the model's JSON units.

#include "MidiRemote/RemoteModel.h"

namespace synth::detail {

/** Writes `address` and `argIndex` for an osc spec. */
void writeOscSpecFields(const MessageSpec& spec, juce::DynamicObject& obj);

/** Reads and validates the osc fields of `obj` into `spec` (whose type is already osc): channel and number must be
 *  0, the address must pass isValidOscAddress, `argIndex` is 0..kMaxOscArgIndex (missing = 0). False on any
 *  violation. */
bool readOscSpecFields(const juce::DynamicObject& obj, MessageSpec& spec);

/** Writes the control's `inRange` (osc controls only). */
void writeInRange(const Control& control, juce::DynamicObject& obj);

/** Reads `inRange` into `control` (missing = 0..1); a present one needs two finite numbers. */
bool readInRange(const juce::DynamicObject& obj, Control& control);

} // namespace synth::detail
