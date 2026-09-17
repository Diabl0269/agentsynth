#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** The note-division list shared by every stage's *Div choice parameter on ADSRModule when
 *  `tempoSync` is on. Deliberately the SAME six entries, in the SAME order, as LFOModule's
 *  `rateSync` choice (FRO113) -- one set of division names across the app, not a per-module
 *  invention. Local to the envelope rather than shared code with LFOModule.h: six strings and
 *  a beats table is not worth a cross-file dependency for.
 */
inline const juce::StringArray& envelopeNoteDivisions() {
    static const juce::StringArray divisions{"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"};
    return divisions;
}

/** Beats spanned by `envelopeNoteDivisions()[index]` -- 1/1 is a whole note (4 beats) down to
 *  1/32. Mirrors LFOModule::processBlock's `subdivision` switch exactly.
 */
inline float envelopeNoteDivisionBeats(int index) noexcept {
    switch (index) {
    case 0:
        return 4.0f; // 1/1
    case 1:
        return 2.0f; // 1/2
    case 2:
        return 1.0f; // 1/4
    case 3:
        return 0.5f; // 1/8
    case 4:
        return 0.25f; // 1/16
    case 5:
    default:
        return 0.125f; // 1/32
    }
}

/** A tempo-synced stage length in seconds, for the given division index at the given bpm.
 *  `bpm` is floored to a small positive value so a pathological (zero/negative) transport bpm
 *  can never divide-by-zero or return a negative/non-finite stage time.
 */
inline float envelopeNoteDivisionSeconds(int index, double bpm) noexcept {
    const double safeBpm = bpm > 1.0 ? bpm : 1.0;
    const double secondsPerBeat = 60.0 / safeBpm;
    return static_cast<float>(envelopeNoteDivisionBeats(index) * secondsPerBeat);
}

} // namespace synth
