#pragma once

#include "Music/MusicalScale.h"
#include <array>
#include <cstdint>
#include <juce_core/juce_core.h>
#include <optional>

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

/** A Tune or Pitch knob that follows a scale: its value is the interval in semitones above the key's root, so 0 is the
    root and only the scale's own intervals are allowed (a major scale: 0, 2, 4, 5, 7, 9, 11 and the same an octave
    away). No key, or a chromatic one, leaves every semitone allowed. Shared by every knob that adopts it. */
bool tuneFollowsKey(const std::optional<MusicalScale>& key) noexcept;
/** `semitones` landed on the nearest allowed interval inside [lo, hi] (a tie goes down); unchanged without a key. */
double snapTuneToKey(double semitones, const std::optional<MusicalScale>& key, double lo, double hi) noexcept;
/** The next allowed interval one scale step from `current` in `direction` (+1 up, -1 down) inside [lo, hi]; nullopt
    without a key (the knob's usual step applies) and `current` snapped when no step is left that way. */
std::optional<double> stepTuneInKey(double current, int direction, const std::optional<MusicalScale>& key, double lo,
                                    double hi) noexcept;

} // namespace synth
