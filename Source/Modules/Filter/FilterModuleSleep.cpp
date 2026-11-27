// FilterModuleSleep.cpp -- the Filter's sleep on silent input: a mono ladder type whose input, CV and ladder state are
// all exactly zero renders nothing, and leaves everything the full render would leave. See Modules/CLAUDE.md.
#include "Modules/FilterModule.h"
#include "Modules/RenderShortcuts.h"

namespace synth {

/** Audio thread. True when the whole ladder is at rest: every stage exactly zero and neither smoother still moving. */
bool ResonantLadder::isAtRest() const noexcept {
    for (const auto& stages : state_)
        for (const float v : stages)
            if (v != 0.0f)
                return false;
    return !cutoffSmoother_.isSmoothing() && !feedbackSmoother_.isSmoothing();
}

} // namespace synth

/** Called by ModuleBase::processBlock for a block with no bypass, mute, fade, Mix/Pan or MIDI work. Sleeps only at a
    fixpoint of the mono ladder path (LPF/HPF/BPF, 12 or 24 dB): audio on both legs, the three parameter CV jacks and
    Pitch all exactly zero for the whole block, the ladder's stages and smoothers at rest, and the cutoff and
    resonance this block would resolve to already applied to the ladder. Then the full render would output zeros and
    change nothing but the visuals and atomics replayed below. Notch (its state is not readable), Custom and poly
    never sleep, and a cutoff or resonance still ramping, or a ringing tail, runs the full path until it settles. */
bool FilterModule::renderSilentBlock(juce::AudioBuffer<float>& buffer) noexcept {
    const int numSamples = buffer.getNumSamples();
    const int type = filterTypeParam->getIndex();
    if (numSamples == 0 || numSamples > kMaxBlock || polyParam->get() || type > 5)
        return false;
    if (!channelsSilent(buffer, 0, 1) || !channelsSilent(buffer, kRightBase, 1) ||
        !channelsSilent(buffer, cvChannelFor(0, false), kNumCVInputs) || !channelsSilent(buffer, kPitchBase, 1))
        return false;

    // The same target writes the full render makes first; repeating them in the full path changes nothing.
    smoothedCutoff.setTargetValue(*cutoffParam);
    smoothedResonance.setTargetValue(*resonanceParam);
    smoothedDrive.setTargetValue(*driveParam);
    smoothedKeyTrack.setTargetValue(
        0.01f * modulateNormalised(*keyTrackParam, keyTrackParam->get(), blockCV(buffer, kKeyTrackCVChannel)));
    applyFilterType(type);
    if (smoothedCutoff.isSmoothing() || smoothedResonance.isSmoothing() || smoothedDrive.isSmoothing() ||
        smoothedKeyTrack.isSmoothing() || customWasActive || !ladderDriveWasActive || ladderDriveWasPoly)
        return false;

    // Zero CV and Pitch leave the per-sample cutoff at the smoothed knob value and the resonance at its knob.
    const float cutoff = juce::jlimit(20.0f, 20000.0f, smoothedCutoff.getCurrentValue());
    const float resonance = juce::jlimit(0.0f, 1.0f, smoothedResonance.getCurrentValue());
    if (ladderSetters[0][0].cutoffHz != cutoff || ladderSetters[0][0].resonance != resonance ||
        !ladders[0][0].isAtRest() || !outputLevelSettled())
        return false;

    voicePitchSeen[0] = 0.0f;
    modulatedCutoff.store(*cutoffParam, std::memory_order_relaxed);
    resonanceCV.store(0.0f, std::memory_order_relaxed);
    driveCV.store(0.0f, std::memory_order_relaxed);
    pushVisuals(buffer, numSamples);
    // The jacks the full render clears so CV never leaks downstream.
    for (int ch = cvChannelFor(0, false); ch < kRightBase && ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
    for (int ch = kPitchBase; ch < kNumChannels && ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
    synth::RenderShortcuts::noteSleep();
    return true;
}
