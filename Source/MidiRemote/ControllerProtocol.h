#pragma once

// Which wire protocol a controller profile speaks (docs/control/midi-remote.md#protocol-surfaces). `generic` is
// the learned-controls profile every older file is; a protocol profile (Mackie Control) has a fixed
// layout and no controls[] of its own.

#include <juce_core/juce_core.h>

namespace synth {

enum class ControllerProtocol { generic, mcu };

/** The JSON spelling: "generic" / "mcu". */
juce::String controllerProtocolToString(ControllerProtocol protocol);
/** False (and `out` untouched) for any other string. */
bool controllerProtocolFromString(const juce::String& text, ControllerProtocol& out);

/** Writes "protocol" only for a non-generic profile, so an older profile round-trips byte-identical. */
void writeControllerProtocol(juce::DynamicObject& profile, ControllerProtocol protocol);
/** Absent = generic; a present non-string or unknown value returns false (the whole load is rejected). */
bool readControllerProtocol(const juce::var& value, ControllerProtocol& out);

} // namespace synth
