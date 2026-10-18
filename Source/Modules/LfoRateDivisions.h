#pragma once

#include "Envelope/EnvelopeTempoSync.h"

namespace synth {

namespace detail {

inline const NoteDivisionTable& lfoRateDivisionTable() {
    static const NoteDivisionTable table = [] {
        NoteDivisionTable t;
        t.addKind(NoteKind::straight);
        for (const int bars : {2, 4, 8}) // "2/1", "4/1", "8/1": 2, 4 and 8 bars of 4/4
            t.add({NoteKind::straight, bars, 1});
        t.addKind(NoteKind::triplet);
        t.addKind(NoteKind::dotted);
        return t;
    }();
    return table;
}

} // namespace detail

/** The LFO's own sync-rate list: the eight straight values of the app-wide `envelopeNoteDivisions()`, then three
 *  longer cycles ("2/1", "4/1", "8/1" = 2, 4 and 8 bars of 4/4, so a drawn lane range can become one slow LFO
 *  cycle), then the eight triplets and eight dotted values ("1/128T".."1/1T", "1/128D".."1/1D", the same names and
 *  order as the ADSR and Delay lists). Saved projects store the choice NAME, so appending never changes what an old
 *  project means; the index (and a plugin host's normalised value) does shift, which ModuleBase handles via
 *  legacyChoiceCount().
 */
inline const juce::StringArray& lfoRateDivisions() { return detail::lfoRateDivisionTable().names; }

/** Number of entries the LFO's rate list had before "2/1".."8/1" (and the triplets and dotted values) were appended. */
constexpr int kLfoLegacyRateDivisionCount = kLegacyNoteDivisionCount;

/** Number of straight entries at the front of lfoRateDivisions(): "1/128".."1/1" and "2/1".."8/1". */
constexpr int kLfoStraightRateDivisionCount = 11;

/** Beats in one LFO cycle at `lfoRateDivisions()[index]`: 1/128 = 1/32 beat ... 1/1 = 4, 2/1 = 8, 4/1 = 16, 8/1 = 32,
 *  a triplet two thirds and a dotted value one and a half times its straight length. Out-of-range clamps to the
 *  nearest end. */
inline float lfoRateDivisionBeats(int index) noexcept {
    const auto& beats = detail::lfoRateDivisionTable().beats;
    return beats[static_cast<size_t>(detail::clampDivisionIndex(index, static_cast<int>(beats.size())))];
}

} // namespace synth
