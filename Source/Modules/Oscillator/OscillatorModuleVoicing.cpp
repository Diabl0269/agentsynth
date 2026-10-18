// OscillatorModuleVoicing.cpp -- the unison stack's per-block mix and detune ratios.
// The class and channel map: OscillatorModule.h; the rendering loops: OscillatorModuleMono.cpp,
// OscillatorModulePoly.cpp. The maths is shared with the Wavetable in UnisonVoicing.h.

#include "Modules/OscillatorModule.h"

void OscillatorModule::fillUnisonRatios(float* ratios, int unisonCount, float detuneCents) const {
    for (int u = 0; u < unisonCount; ++u)
        ratios[u] = synth::unisonDetuneRatio(detuneCents, u, unisonCount);
}

// Every unison voice carries the same weight and the stack renders to the left leg only: each left
// weight is exactly 1 and its step exactly 0, so the stack is a plain average of the voices.
void OscillatorModule::prepareUnisonMix(int unisonCount, int numSamples) {
    juce::ignoreUnused(numSamples);
    unisonMix.stereo = false;
    for (int u = 0; u < unisonCount; ++u) {
        unisonMix.leftStart[u] = 1.0f;
        unisonMix.leftStep[u] = 0.0f;
    }
    unisonMix.sumStart = (float)unisonCount;
    unisonMix.sumStep = 0.0f;
}
