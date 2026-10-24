// UnisonMix.h -- the per-voice gains of a unison stack as they move from block to block. Each voice has a target
// gain per stereo leg; the gain it plays moves towards it no faster than 1.0 per 10 ms and ramps linearly inside
// the block, so an automated Blend or Width glides and a voice that enters or leaves fades instead of stepping.
// Header-only, no allocation, safe on the audio thread. docs/modules/wavetable.md#unison-stacking-and-the-sub.
#pragma once

#include "UnisonVoicing.h"

namespace synth {

struct UnisonMix {
    /** Seconds a voice gain takes to move by 1.0. */
    static constexpr double kFadeSeconds = 0.01;

    float leftStart[kMaxUnisonVoices]{}; ///< each voice's gain at the first sample of the block, per leg
    float leftStep[kMaxUnisonVoices]{};  ///< and the change per sample
    float rightStart[kMaxUnisonVoices]{};
    float rightStep[kMaxUnisonVoices]{};
    float sumStart = 1.0f; ///< summed mean leg power at the first sample: the stack divides by its root
    float sumStep = 0.0f;
    int renderCount = 1; ///< voices that still sound, leaving ones included
    bool primed = false; ///< false until the first block after prepareToPlay sets the gains to their targets
    float reachedL[kMaxUnisonVoices]{};
    float reachedR[kMaxUnisonVoices]{};

    /** Moves every gain towards `targetL` / `targetR` (0 for a voice beyond `unisonCount`) for a block of
        `numSamples`. */
    void advance(const float* targetL, const float* targetR, int unisonCount, int numSamples, double sampleRate) {
        const float maxMove = (float)((double)numSamples / (kFadeSeconds * sampleRate));
        const float perSample = 1.0f / (float)std::max(1, numSamples);
        float powerStart = 0.0f;
        float powerEnd = 0.0f;
        renderCount = unisonCount;
        for (int u = 0; u < kMaxUnisonVoices; ++u) {
            if (!primed) {
                reachedL[u] = targetL[u];
                reachedR[u] = targetR[u];
            }
            const float l0 = reachedL[u];
            const float r0 = reachedR[u];
            const float l1 = l0 + std::clamp(targetL[u] - l0, -maxMove, maxMove);
            const float r1 = r0 + std::clamp(targetR[u] - r0, -maxMove, maxMove);
            reachedL[u] = l1;
            reachedR[u] = r1;
            leftStart[u] = l0;
            leftStep[u] = (l1 - l0) * perSample;
            rightStart[u] = r0;
            rightStep[u] = (r1 - r0) * perSample;
            powerStart += unisonLegPower(l0, r0);
            powerEnd += unisonLegPower(l1, r1);
            if (l0 != 0.0f || r0 != 0.0f || l1 != 0.0f || r1 != 0.0f)
                renderCount = std::max(renderCount, u + 1);
        }
        primed = true;
        sumStart = std::max(powerStart, 1e-12f);
        sumStep = (std::max(powerEnd, 1e-12f) - sumStart) * perSample;
    }
};

} // namespace synth
