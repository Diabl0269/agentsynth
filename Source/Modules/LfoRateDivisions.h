#pragma once

#include "Envelope/EnvelopeTempoSync.h"

namespace synth {

/** The LFO's own sync-rate list: the app-wide `envelopeNoteDivisions()` plus three longer cycles
 *  ("2/1", "4/1", "8/1" = 2, 4 and 8 bars of 4/4) appended, so a drawn lane range can become one
 *  slow LFO cycle. ADSR and Delay keep the shorter list. Saved projects store the choice NAME, so
 *  appending never changes what an old project means; the index (and a plugin host's normalised
 *  value) does shift, which ModuleBase handles via legacyChoiceCount().
 */
inline const juce::StringArray& lfoRateDivisions() {
    static const juce::StringArray divisions = [] {
        juce::StringArray d = envelopeNoteDivisions();
        d.add("2/1");
        d.add("4/1");
        d.add("8/1");
        return d;
    }();
    return divisions;
}

/** Number of entries the LFO's rate list had before "2/1".."8/1" were appended. */
constexpr int kLfoLegacyRateDivisionCount = 8;

/** Beats in one LFO cycle at `lfoRateDivisions()[index]`: 1/128 = 1/32 beat ... 1/1 = 4, 2/1 = 8,
 *  4/1 = 16, 8/1 = 32. Out-of-range clamps to the nearest end. */
inline float lfoRateDivisionBeats(int index) noexcept {
    const int last = lfoRateDivisions().size() - 1;
    const int clamped = index < 0 ? 0 : (index > last ? last : index);
    return 0.03125f * static_cast<float>(1 << clamped);
}

} // namespace synth
