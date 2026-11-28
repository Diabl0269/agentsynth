// VCAModuleSleep.cpp -- the VCA's sleep on silent input, kept out of VCAModule.h so the render-shortcut switch is not
// reached by every file that includes the VCA. See Modules/CLAUDE.md.
#include "Modules/RenderShortcuts.h"
#include "Modules/VCAModule.h"

/** Exact-zero audio through a gain that is not ramping is zero whatever the CV says, and a steady smoother's
    getNextValue changes nothing, so the per-sample loop is skipped; the mono duplicate and the view push stay. */
bool VCAModule::sleepMonoSilentBlock(juce::AudioBuffer<float>& buffer, int numChannels, int numSamples) noexcept {
    if (!synth::RenderShortcuts::enabled() || smoothedGain.isSmoothing() || !channelsSilent(buffer, 0, 1) ||
        !channelsSilent(buffer, kRightBase, 1))
        return false;
    if (numChannels > 1)
        buffer.copyFrom(1, 0, buffer, 0, 0, numSamples);
    if (auto* vb = getVisualBuffer())
        vb->pushLegs(buffer.getArrayOfReadPointers(), buffer.getNumChannels(), isDualIO() ? rightAudioLegChannel() : 0,
                     numSamples);
    synth::RenderShortcuts::noteSleep();
    return true;
}
