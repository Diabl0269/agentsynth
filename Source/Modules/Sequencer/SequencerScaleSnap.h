#pragma once

#include "Music/MusicalScale.h"
#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>

namespace synth {

/** The Scale choice list: "No scale", the built-in presets in their order, then "Custom". */
const juce::StringArray& sequencerScaleChoices();
/** Index of "Custom" in sequencerScaleChoices(). */
int sequencerCustomScaleIndex();

/** The scale a sequencer plays through. `scaleIndex` 0 is identity, "Custom" uses `customMask`; out-of-range clamps. */
MusicalScale sequencerScale(int rootPitchClass, int scaleIndex, std::uint16_t customMask);

/** Audio-thread snap table, rebuilt only when the root, scale or custom mask changed since the last update(). */
class SequencerScaleSnap {
public:
    /** Audio thread: allocation-free. */
    void update(int rootPitchClass, int scaleIndex, std::uint16_t customMask) noexcept;
    int snap(int pitch) const noexcept { return table_[(size_t)std::clamp(pitch, 0, 127)]; }
    const MusicalScale& scale() const noexcept { return scale_; }

private:
    int root_ = -1;
    int index_ = -1;
    std::uint16_t mask_ = 0;
    MusicalScale scale_;
    std::array<std::int8_t, 128> table_{};
};

/** Message thread: the pitch a step actually plays, for a card that shows it next to the written one. */
int sequencerPlayedPitch(int writtenPitch, int transpose, int rootPitchClass, int scaleIndex, std::uint16_t customMask);

} // namespace synth
