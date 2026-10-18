#pragma once

// Sidechain: a detector key input (Compressor/Gate "Key"). It carries audio, but only to steer the
// module's detector, so it is NOT part of the signal path the mixer walks (isSignalPathInputRole).
enum class PortRole { Audio, ModCV, Pitch, Gate, Midi, Other, Sidechain };

/** True when a cable landing on an input of this role carries the signal onward — false for a
    parameter CV (ModCV) and a detector key (Sidechain). The mixer's graph walks (bus
    classification, track/channel reach, Make Channel) share this rule. */
inline bool isSignalPathInputRole(PortRole role) { return role != PortRole::ModCV && role != PortRole::Sidechain; }

/** True when any of the `numSamples` samples is non-zero (null reads as silent): the "is this CV jack
    patched?" probe. It scans the whole block and exits on the first live sample, so an envelope that
    idles at 0 and fires mid-block is still seen. */
inline bool hasAnyNonZero(const float* data, int numSamples) noexcept {
    if (data == nullptr)
        return false;
    for (int s = 0; s < numSamples; ++s)
        if (data[s] != 0.0f)
            return true;
    return false;
}
