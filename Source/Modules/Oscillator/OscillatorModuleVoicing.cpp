// OscillatorModuleVoicing.cpp -- the unison stack's per-block mix (Blend, Width, voice count) and detune
// ratios. The class and channel map: OscillatorModule.h; the rendering loops: OscillatorModuleMono.cpp,
// OscillatorModulePoly.cpp. The maths is shared with the Wavetable in UnisonVoicing.h.

#include "Modules/OscillatorModule.h"

namespace {
/** Seconds a voice gain takes to move by 1.0. */
constexpr double kUnisonFadeSeconds = 0.01;
} // namespace

// A voice that is leaving (u >= targetCount) keeps the detune of the outermost voice that stays, so it
// fades out in tune rather than at a pitch the shorter stack no longer has.
void OscillatorModule::fillUnisonRatios(float* ratios, int targetCount, int renderCount, float detuneCents) const {
    for (int u = 0; u < renderCount; ++u)
        ratios[u] = synth::unisonDetuneRatio(detuneCents, std::min(u, targetCount - 1), targetCount);
}

// Every voice has a target gain on each leg: its Blend level times its Width pan gain, or 0 once Unison
// no longer includes it. The gain a voice actually plays moves towards the target no faster than
// 1.0 per 10 ms, and the per-sample weights ramp linearly between the block's start and end values. So
// an automated Blend or Width glides, and a voice that enters or leaves fades in or out instead of
// stepping; the voices that stay change their role (centre, outer, side) the same way. The stack is
// divided by the square root of the sum of the voices' mean leg power as it stands at each moment, so the
// normalisation follows the fades and uncorrelated voices keep the loudness of one voice. One centred
// voice divides by exactly 1. `blend` and `width` are the knob values after their CV.
void OscillatorModule::prepareUnisonMix(int unisonCount, int numSamples, float blend, float width) {
    const float maxMove = (float)((double)numSamples / (kUnisonFadeSeconds * currentSampleRate));
    const float perSample = 1.0f / (float)std::max(1, numSamples);

    float sumStart = 0.0f;
    float sumEnd = 0.0f;
    int renderCount = unisonCount;
    bool stereo = false;
    for (int u = 0; u < MAX_UNISON; ++u) {
        float targetL = 0.0f;
        float targetR = 0.0f;
        if (u < unisonCount) {
            float panL, panR;
            synth::unisonPanGains(u, unisonCount, width, panL, panR);
            const float level = synth::unisonBlendGain(u, unisonCount, blend);
            targetL = level * panL;
            targetR = level * panR;
        }
        if (!unisonMix.primed) {
            unisonMix.reachedL[u] = targetL;
            unisonMix.reachedR[u] = targetR;
        }
        const float leftStart = unisonMix.reachedL[u];
        const float rightStart = unisonMix.reachedR[u];
        const float leftEnd = leftStart + std::clamp(targetL - leftStart, -maxMove, maxMove);
        const float rightEnd = rightStart + std::clamp(targetR - rightStart, -maxMove, maxMove);
        unisonMix.reachedL[u] = leftEnd;
        unisonMix.reachedR[u] = rightEnd;

        unisonMix.leftStart[u] = leftStart;
        unisonMix.leftStep[u] = (leftEnd - leftStart) * perSample;
        unisonMix.rightStart[u] = rightStart;
        unisonMix.rightStep[u] = (rightEnd - rightStart) * perSample;
        sumStart += synth::unisonLegPower(leftStart, rightStart);
        sumEnd += synth::unisonLegPower(leftEnd, rightEnd);
        stereo = stereo || leftStart != rightStart || leftEnd != rightEnd;
        if (leftStart != 0.0f || rightStart != 0.0f || leftEnd != 0.0f || rightEnd != 0.0f)
            renderCount = std::max(renderCount, u + 1);
    }
    unisonMix.primed = true;
    unisonMix.stereo = stereo;
    unisonMix.renderCount = renderCount;
    unisonMix.sumStart = std::max(sumStart, 1e-12f);
    unisonMix.sumStep = (std::max(sumEnd, 1e-12f) - unisonMix.sumStart) * perSample;
}
