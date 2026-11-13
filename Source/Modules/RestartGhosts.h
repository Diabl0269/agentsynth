// RestartGhosts.h -- the run-out stacks of a restarted oscillator voice. When a voice restarts while its previous
// note may still sound, the old state keeps running as a "ghost" and fades out along restartFadeIn's raised
// cosine while the restarted state comes in. Header-only, no allocation, safe on the audio thread.
// docs/modules/wavetable.md#phase-control, docs/modules/oscillator-voice.md#phase-and-rand.
#pragma once

#include "RestartFade.h"

namespace synth {

/** Up to `kMax` copies of a voice's state (`State`) that are running out. The shares of the ghosts and of the
    current state always add to one, so a restart inside a hand-over cannot step the output. */
template <typename State, int kMax = 5>
struct RestartGhosts {
    struct Ghost {
        State state;
        float weight = 0.0f;              ///< share of the output when this ghost began to fade
        int remaining = 0;                ///< samples of the fade still to run
        int length = kRestartFadeSamples; ///< samples the whole fade takes
        float lead = 0.0f;                ///< how far into its first sample the fade had already begun
    };

    Ghost items[kMax];
    int count = 0;

    /** Share of the output that `g` still holds at this sample. */
    static float share(const Ghost& g) {
        return g.weight * (1.0f - restartFadeIn((float)g.remaining - g.lead, g.length));
    }

    /** Starts a ghost from `current`, with the share the interrupted state holds now (all of it when nothing was
        fading), leaving over `length` samples. `lead` is how far into this sample the restart really fell. Past
        `kMax` the quietest ghost is dropped and its share goes to the current state. */
    void start(const State& current, int length = kRestartFadeSamples, float lead = 0.0f) {
        float total = 0.0f;
        int quietest = 0;
        float quietestShare = 2.0f;
        for (int g = 0; g < count; ++g) {
            const float now = share(items[g]);
            total += now;
            if (now < quietestShare) {
                quietestShare = now;
                quietest = g;
            }
        }
        if (count == kMax) {
            total -= quietestShare;
            items[quietest] = items[--count];
        }
        Ghost& added = items[count++];
        added.state = current;
        added.weight = 1.0f - total;
        added.remaining = length;
        added.length = length;
        added.lead = lead;
    }

    /** Drops the ghosts whose fade has finished. */
    void retireFinished() {
        for (int g = count - 1; g >= 0; --g)
            if (items[g].remaining <= 0)
                items[g] = items[--count];
    }
};

} // namespace synth
