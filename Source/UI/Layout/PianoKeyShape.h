#pragma once

// The one shape every horizontal piano keyboard in the app draws its keys with: black keys are this share of a white
// key's height (and sit on top of it). The sampler's key strip and map window, the shape editor's key strip, the Key
// Picker and the note range picker all use it, so no keyboard shows black keys running to the bottom.

namespace synth::ui::pianokeys {

constexpr float kBlackKeyHeightShare = 0.62f;

constexpr bool isBlackPitchClass(int pitchClass) noexcept {
    return pitchClass == 1 || pitchClass == 3 || pitchClass == 6 || pitchClass == 8 || pitchClass == 10;
}

} // namespace synth::ui::pianokeys
