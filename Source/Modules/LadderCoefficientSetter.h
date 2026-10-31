#pragma once

#include <limits>

namespace synth {

/** Feeds a ladder (synth::ResonantLadder or juce::dsp::LadderFilter) its cutoff, resonance and drive, skipping any
    setter whose argument equals the last one this setter applied. Every ladder setter recomputes a transcendental on
    each call (cutoff an exp, drive two pows) and, for an unchanged value, then changes nothing -- the cutoff and
   resonance smoothers ignore an equal target and drive is a pure function of its argument -- so skipping the repeat is
   bit-identical while a static knob costs a per-sample loop no transcendentals at all. One setter per ladder; every
   call to that ladder's three setters must go through it, and a default-constructed setter (NaN, never equal)
   re-applies everything, so assign `{}` whenever the ladder is re-prepared. */
struct LadderCoefficientSetter {
    float cutoffHz = std::numeric_limits<float>::quiet_NaN();
    float resonance = std::numeric_limits<float>::quiet_NaN();
    float drive = std::numeric_limits<float>::quiet_NaN();

    template <typename Ladder>
    void setCutoff(Ladder& ladder, float hz) noexcept {
        if (hz != cutoffHz) {
            ladder.setCutoffFrequencyHz(hz);
            cutoffHz = hz;
        }
    }
    template <typename Ladder>
    void setResonance(Ladder& ladder, float value) noexcept {
        if (value != resonance) {
            ladder.setResonance(value);
            resonance = value;
        }
    }
    template <typename Ladder>
    void setDrive(Ladder& ladder, float value) noexcept {
        if (value != drive) {
            ladder.setDrive(value);
            drive = value;
        }
    }
};

} // namespace synth
