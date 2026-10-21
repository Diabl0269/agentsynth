#pragma once

#include "Music/MusicalScale.h"
#include "SequencerPattern.h"

namespace synth {

/** Pure edits of a sequencer pattern, one per card tool. Each takes the pattern and the number of active steps `n`
 *  (clamped to 1..128) and returns a new pattern; only steps `[0, n)` change, everything else is copied as it was.
 *  Message thread only. */
namespace sequencer_ops {

/** Rotates every field of the active steps `places` to the left; the first steps wrap to the end. */
SequencerPattern shiftLeft(const SequencerPattern& pattern, int n, int places = 1);

/** Rotates every field of the active steps `places` to the right; the last steps wrap to the start. */
SequencerPattern shiftRight(const SequencerPattern& pattern, int n, int places = 1);

/** Reverses the order of the active steps, every field together. */
SequencerPattern reversed(const SequencerPattern& pattern, int n);

/** True when `n` steps can be doubled without passing the 128 step limit. */
bool canDouble(int n);

/** Copies steps `[0, n)` into `[n, 2n)`. The caller then sets Steps to 2n. Returns the pattern unchanged when `n`
 *  cannot be doubled. */
SequencerPattern doubled(const SequencerPattern& pattern, int n);

/** Moves every active pitch one scale note up (`direction` >= 0) or down (`direction` < 0). With a chromatic scale it
 *  is one semitone, clamped to 0..127. */
SequencerPattern movedByScaleStep(const SequencerPattern& pattern, int n, const MusicalScale& scale, int direction);

/** Rewrites every active pitch to its nearest scale note. */
SequencerPattern fitToScale(const SequencerPattern& pattern, int n, const MusicalScale& scale);

/** True when `fitToScale` would change at least one active pitch (never true for a chromatic scale). */
bool fitToScaleWouldChange(const SequencerPattern& pattern, int n, const MusicalScale& scale);

/** What Randomize may change. `density` is the chance 0..1 that a step plays; with `keepRhythm` the on/off state and
 *  gate stay exactly as written and `density` is ignored. */
struct RandomizeSettings {
    int minPitch = 36;
    int maxPitch = 60;
    float density = 0.75f;
    bool keepRhythm = true;
};

/** Gives every active step a random in-scale pitch inside the range (`pickPitches`). Unless `keepRhythm`, each step
 *  plays with probability `density` and at least one always does. The same `rng` state gives the same pattern. */
SequencerPattern randomized(const SequencerPattern& pattern, int n, const MusicalScale& scale,
                            const RandomizeSettings& settings, juce::Random& rng);

} // namespace sequencer_ops
} // namespace synth
