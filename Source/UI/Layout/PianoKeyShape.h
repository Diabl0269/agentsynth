#pragma once

// The one shape every horizontal piano keyboard in the app draws its keys with: black keys are this share of a white
// key's height (and sit on top of it). The sampler's key strip and map window, the shape editor's key strip, the Key
// Picker and the note range picker all use it, so no keyboard shows black keys running to the bottom.

#include <algorithm>
#include <cmath>

namespace synth::ui::pianokeys {

constexpr float kBlackKeyHeightShare = 0.62f;

// How hard an on-screen key sounds where it is pressed: softest at the top, loudest at the front (bottom) edge, the
// way GarageBand, Logic's and Cubase's on-screen keyboards answer. The sampler's card strip and its map window share
// it.
constexpr int kSoftestKeyVelocity = 64;
constexpr int kLoudestKeyVelocity = 127;

/** `depth` is 0 at the top of the key and 1 at its front edge; values outside are clamped. */
inline int velocityAtKeyDepth(float depth) noexcept {
    const float down = std::clamp(depth, 0.0f, 1.0f);
    return kSoftestKeyVelocity + (int)std::lround(down * (float)(kLoudestKeyVelocity - kSoftestKeyVelocity));
}

constexpr bool isBlackPitchClass(int pitchClass) noexcept {
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

} // namespace synth::ui::pianokeys
