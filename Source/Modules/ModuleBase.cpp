#include "ModuleBase.h"

#include "AudioEngine/RealtimeScope.h"
#include "RenderShortcuts.h"

void ModuleBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // Defined here, not inline in the header, so the sanitizer header is not reached by every file that includes
    // ModuleBase.h.
    const synth::RealtimeModuleScope realtimeScope; // run-rtsan build only: this module's render is checked
    if (auto* inbox = noteInbox_.load(std::memory_order_acquire))
        inbox->drainInto(midiMessages, buffer.getNumSamples());
    applyInputDownMix(buffer);
    latchOutputStageCv(buffer);
    const bool paramBypass = bypassedParam->get();
    const bool paramMute = mutedParam != nullptr && mutedParam->get();
    if (outputStage_.isIdle(paramBypass, paramMute)) {
        if (synth::RenderShortcuts::enabled() && !paramBypass && !paramMute && midiMessages.isEmpty() &&
            renderSilentBlock(buffer))
            return;
        processModuleBlock(buffer, midiMessages);
    } else
        renderThroughOutputStage(buffer, midiMessages);
}

bool ModuleBase::channelsSilent(const juce::AudioBuffer<float>& buffer, int firstChannel, int numChannels) noexcept {
    const int n = buffer.getNumSamples();
    const int end = juce::jmin(firstChannel + numChannels, buffer.getNumChannels());
    bool anyNonZero = false; // `!= 0` is true for NaN too, so a NaN block never reads as silent
    for (int ch = juce::jmax(0, firstChannel); ch < end; ++ch) {
        const float* data = buffer.getReadPointer(ch);
        for (int i = 0; i < n; ++i)
            anyNonZero |= data[i] != 0.0f;
        if (anyNonZero)
            return false;
    }
    return true;
}

bool ModuleBase::outputLevelSettled() noexcept {
    if (outputLevelParam == nullptr)
        return true;
    smoothedOutputLevel.setTargetValue(outputLevelTarget());
    return !smoothedOutputLevel.isSmoothing();
}
