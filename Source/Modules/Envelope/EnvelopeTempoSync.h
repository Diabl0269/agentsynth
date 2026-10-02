#pragma once

#include <juce_core/juce_core.h>

namespace synth {

/** The ONE tempo-division list for the whole app: ADSR stage lengths (`tempoSync` on), LFO `rateSync`,
 *  Delay `timeDiv` and the timeline-modulator rate picker all offer exactly these names, in this order.
 *  Shortest first, so index 0 / the bottom of a fader is the shortest value (1/128) and the top is
 *  the longest (1/1). Saved projects store the choice NAME ("1/4"), never the index, so reordering or
 *  growing this list never changes what an old project means.
 */
inline const juce::StringArray& envelopeNoteDivisions() {
    static const juce::StringArray divisions{"1/128", "1/64", "1/32", "1/16", "1/8", "1/4", "1/2", "1/1"};
    return divisions;
}

/** Index of `name` in envelopeNoteDivisions() ("1/4" -> 5), or -1. */
inline int envelopeNoteDivisionIndex(const juce::String& name) noexcept {
    return envelopeNoteDivisions().indexOf(name);
}

/** Beats spanned by `envelopeNoteDivisions()[index]` -- 1/1 is a whole note (4 beats) down to
 *  1/128. An out-of-range index clamps to the nearest end.
 */
inline float envelopeNoteDivisionBeats(int index) noexcept {
    const int last = envelopeNoteDivisions().size() - 1;
    const int clamped = index < 0 ? 0 : (index > last ? last : index);
    return 0.03125f * static_cast<float>(1 << clamped); // 1/128 note = 1/32 beat, doubling per step
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
