#include "ModuleBase.h"

#include "AudioEngine/RealtimeScope.h"

void ModuleBase::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // Defined here, not inline in the header, so the sanitizer header is not reached by every file that includes
    // ModuleBase.h.
    const synth::RealtimeModuleScope realtimeScope; // run-rtsan build only: this module's render is checked
    if (auto* inbox = noteInbox_.load(std::memory_order_acquire))
        inbox->drainInto(midiMessages, buffer.getNumSamples());
    applyInputDownMix(buffer);
    latchOutputStageCv(buffer);
    if (outputStage_.isIdle(bypassedParam->get(), mutedParam != nullptr && mutedParam->get()))
        processModuleBlock(buffer, midiMessages);
    else
        renderThroughOutputStage(buffer, midiMessages);
}
