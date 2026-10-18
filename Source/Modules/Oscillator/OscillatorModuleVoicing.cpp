// OscillatorModuleVoicing.cpp -- the unison stack's per-block mix (Blend, Width) and detune ratios.
// The class and channel map: OscillatorModule.h; the rendering loops: OscillatorModuleMono.cpp,
// OscillatorModulePoly.cpp. The maths is shared with the Wavetable in UnisonVoicing.h.

#include "Modules/OscillatorModule.h"

void OscillatorModule::fillUnisonRatios(float* ratios, int unisonCount, float detuneCents) const {
    for (int u = 0; u < unisonCount; ++u)
        ratios[u] = synth::unisonDetuneRatio(detuneCents, u, unisonCount);
}

// Blend and Width are smoothed like Level, and the smoothed value is read at the block's start and
// end; the per-voice weights then ramp linearly between the two inside the block, so an automated
// Blend or Width glides instead of stepping at a block boundary. With a static Blend 1 / Width 0
// the start equals the end, every step is exactly 0 and every weight exactly 1, which keeps the
// render bit-identical to a plain unweighted average.
void OscillatorModule::prepareUnisonMix(int unisonCount, int numSamples) {
    smoothedBlend.setTargetValue(blendParam->get());
    smoothedWidth.setTargetValue(widthParam->get());
    const float blendStart = smoothedBlend.getCurrentValue();
    const float widthStart = smoothedWidth.getCurrentValue();
    const float blendEnd = numSamples > 0 ? smoothedBlend.skip(numSamples) : blendStart;
    const float widthEnd = numSamples > 0 ? smoothedWidth.skip(numSamples) : widthStart;

    const float perSample = 1.0f / (float)std::max(1, numSamples);
    unisonMix.stereo = widthStart > 0.0f || widthEnd > 0.0f;

    float sumStart = 0.0f;
    float sumEnd = 0.0f;
    for (int u = 0; u < unisonCount; ++u) {
        const float gainStart = synth::unisonBlendGain(u, unisonCount, blendStart);
        const float gainEnd = synth::unisonBlendGain(u, unisonCount, blendEnd);
        sumStart += gainStart;
        sumEnd += gainEnd;

        float leftStart = gainStart;
        float leftEnd = gainEnd;
        if (unisonMix.stereo) {
            float panLStart, panRStart, panLEnd, panREnd;
            synth::unisonPanGains(u, unisonCount, widthStart, panLStart, panRStart);
            synth::unisonPanGains(u, unisonCount, widthEnd, panLEnd, panREnd);
            leftStart *= panLStart;
            leftEnd *= panLEnd;
            unisonMix.rightStart[u] = gainStart * panRStart;
            unisonMix.rightStep[u] = (gainEnd * panREnd - unisonMix.rightStart[u]) * perSample;
        }
        unisonMix.leftStart[u] = leftStart;
        unisonMix.leftStep[u] = (leftEnd - leftStart) * perSample;
    }
    unisonMix.sumStart = sumStart;
    unisonMix.sumStep = (sumEnd - sumStart) * perSample;
}
