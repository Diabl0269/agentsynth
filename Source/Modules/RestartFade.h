// RestartFade.h -- the curve a retriggered oscillator uses to hand over from its old sound to its new one.
// Header-only, pure, safe on the audio thread. docs/modules/oscillator-voice.md#phase-and-rand.
#pragma once

#include <algorithm>
#include <cmath>
#include <juce_core/juce_core.h>
#include <limits>

namespace synth {

/** Length in samples of the hand-over after a restart. */
constexpr int kRestartFadeSamples = 64;

/** Length in samples of the hand-over when restarts come at audio rate, each one a cycle of oscillator sync: long
    enough to leave no step, short enough that the synced wave keeps its height and its bright overtones. */
constexpr int kSyncFadeSamples = 8;

/** Restarts this close together (40 Hz) can be oscillator sync, and are, after kSyncRunLength in a row. */
constexpr double kSyncEdgeSeconds = 0.025;
constexpr int kSyncRunLength = 3;

/** What a voice remembers to tell oscillator sync (a master's audio cabled into Restart) from a note. A short gap or
    two is a note whose MIDI and gate edge land close together, or a quick pair of notes, which still take the full
    hand-over; a run of short gaps is sync. */
struct SyncRestartRun {
    int samplesSinceRestart = std::numeric_limits<int>::max(); ///< counts rendered samples, saturating
    int shortGapRun = 0;      ///< restarts in a row each under kSyncEdgeSeconds after the last, up to kSyncRunLength
    float pendingLead = 0.0f; ///< how far past the restart the next rendered sample lies, in samples

    void reset() { *this = SyncRestartRun{}; }

    /** Call once per rendered sample. */
    void tick() {
        if (samplesSinceRestart < std::numeric_limits<int>::max())
            ++samplesSinceRestart;
    }

    /** Records a restart `lead` samples before the sample about to render; true when it is a cycle of sync (the gaps
        were short for kSyncRunLength restarts and there is an old sound to hand over from). */
    bool noteRestart(int syncEdgeSamples, bool fadeFromOld, float lead) {
        shortGapRun = samplesSinceRestart < syncEdgeSamples ? std::min(shortGapRun + 1, kSyncRunLength) : 0;
        const bool syncing = fadeFromOld && shortGapRun >= kSyncRunLength;
        samplesSinceRestart = 0;
        pendingLead = syncing ? lead : 0.0f;
        return syncing;
    }
};

/** How far a fade of `length` samples has run, 0..1, with `remaining` samples of it left (a fraction when the fade
    began between two samples). A raised cosine, so the sum of old and new has no corner where the fade starts or
    ends (a straight ramp leaves a kink there, which is what a short tick is made of). */
inline float restartFadeIn(float remaining, int length = kRestartFadeSamples) {
    const float t = 1.0f - remaining / (float)length;
    return 0.5f - 0.5f * std::cos(juce::MathConstants<float>::pi * t);
}

} // namespace synth
