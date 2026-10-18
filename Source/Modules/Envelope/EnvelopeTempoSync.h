#pragma once

#include "Modules/Envelope/NoteLength.h"
#include <cmath>
#include <juce_core/juce_core.h>
#include <limits>
#include <vector>

namespace synth {

/** The ONE tempo-division list for the whole app: ADSR stage lengths (`tempoSync` on), LFO `rateSync`,
 *  Delay `timeDiv` and the timeline-modulator rate picker all offer these names, in this order: the eight straight
 *  values shortest first ("1/128".."1/1", index 0 / the bottom of a fader is the shortest), then the eight triplets
 *  ("1/128T".."1/1T"), then the eight dotted values ("1/128D".."1/1D"). Saved projects store the choice NAME
 *  ("1/4"), never the index, so growing this list never changes what an old project means; the first eight entries
 *  keep their original indices, which is also what keeps a plugin host's normalised value readable
 *  (kLegacyNoteDivisionCount, ModuleBase::legacyChoiceCount).
 */
namespace detail {

/** One list's names with the beats of each, built together so a lookup never parses a name on the audio thread. */
struct NoteDivisionTable {
    juce::StringArray names;
    std::vector<float> beats;

    void add(const NoteLength& note) {
        names.add(noteLengthName(note));
        beats.push_back(static_cast<float>(noteLengthBeats(note)));
    }
    /** Appends every denominator of `kind` (shortest first), as count 1. */
    void addKind(NoteKind kind) {
        for (const int denominator : noteDenominators())
            add({kind, 1, denominator});
    }
};

inline const NoteDivisionTable& envelopeNoteDivisionTable() {
    static const NoteDivisionTable table = [] {
        NoteDivisionTable t;
        t.addKind(NoteKind::straight);
        t.addKind(NoteKind::triplet);
        t.addKind(NoteKind::dotted);
        return t;
    }();
    return table;
}

/** Index clamped into [0, last]. */
inline int clampDivisionIndex(int index, int count) noexcept {
    return index < 0 ? 0 : (index >= count ? count - 1 : index);
}

} // namespace detail

/** Number of entries the list had before the triplets and dotted values were appended: a plugin host's normalised
 *  value saved before that is decoded over this many choices. */
constexpr int kLegacyNoteDivisionCount = 8;

inline const juce::StringArray& envelopeNoteDivisions() { return detail::envelopeNoteDivisionTable().names; }

/** Index of `name` in envelopeNoteDivisions() ("1/4" -> 5), or -1. */
inline int envelopeNoteDivisionIndex(const juce::String& name) noexcept {
    return envelopeNoteDivisions().indexOf(name);
}

/** Beats spanned by `envelopeNoteDivisions()[index]` -- 1/1 is a whole note (4 beats) down to 1/128, a triplet is
 *  two thirds of its straight length and a dotted value one and a half times. An out-of-range index clamps to the
 *  nearest end.
 */
inline float envelopeNoteDivisionBeats(int index) noexcept {
    const auto& beats = detail::envelopeNoteDivisionTable().beats;
    return beats[static_cast<size_t>(detail::clampDivisionIndex(index, static_cast<int>(beats.size())))];
}

/** The most beats any entry of envelopeNoteDivisions() spans ("1/1D", 6): the ceiling of a stage length. */
inline float envelopeLongestDivisionBeats() noexcept {
    float longest = 0.0f;
    for (const float beats : detail::envelopeNoteDivisionTable().beats)
        longest = beats > longest ? beats : longest;
    return longest;
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

// Nearest note-division index to `seconds` at `bpm` among the divisions of the same kind (straight, triplet or dotted)
// as `currentIndex`, compared in log-time so e.g. a duration halfway (on a musical, not linear, scale) between 1/4 and
// 1/8 rounds to whichever it is actually closer to perceptually. Staying in the stage's kind keeps a drag from turning
// a triplet stage into a straight one. `seconds` is floored well above 0 so a degenerate (0 ms) duration never takes
// log() of zero.
inline int nearestEnvelopeDivisionIndex(double seconds, double bpm, int currentIndex) {
    const double logSeconds = std::log(juce::jmax(1.0e-6, seconds));
    const auto& names = envelopeNoteDivisions();
    const auto kind = noteKindOf(names[juce::jlimit(0, names.size() - 1, currentIndex)]);
    int best = juce::jlimit(0, names.size() - 1, currentIndex);
    double bestDist = std::numeric_limits<double>::infinity();
    for (int i = 0; i < names.size(); ++i) {
        if (noteKindOf(names[i]) != kind)
            continue;
        const double candidate = (double)envelopeNoteDivisionSeconds(i, bpm);
        const double dist = std::abs(logSeconds - std::log(juce::jmax(1.0e-6, candidate)));
        if (dist < bestDist) {
            bestDist = dist;
            best = i;
        }
    }
    return best;
}

} // namespace synth
