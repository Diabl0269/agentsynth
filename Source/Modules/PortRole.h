#pragma once

// Sidechain: a detector key input (Compressor/Gate "Key"). It carries audio, but only to steer the
// module's detector, so it is NOT part of the signal path the mixer walks (isSignalPathInputRole).
enum class PortRole { Audio, ModCV, Pitch, Gate, Midi, Other, Sidechain };

/** True when a cable landing on an input of this role carries the signal onward — false for a
    parameter CV (ModCV) and a detector key (Sidechain). The mixer's graph walks (bus
    classification, track/channel reach, Make Channel) share this rule. */
inline bool isSignalPathInputRole(PortRole role) { return role != PortRole::ModCV && role != PortRole::Sidechain; }
