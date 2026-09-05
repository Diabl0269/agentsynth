#pragma once

#include "ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Macro MIDI Out" — a MIDI outlet jack on a Macro's boundary (P8-15 Macro I/O,
 * docs/macros.md §5).
 *
 * The exact mirror of MacroMidiInletModule in the other direction — see that class's comment for
 * the full reasoning (why this is a separate type from MacroOutletModule rather than a "kind"
 * flag, and why bypass only gates the activity LED here).
 *
 * INTERNAL-ONLY, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module
 * library, not offered by the replace menu, never authorable by a model
 * (kNonAuthorableModuleTypes, docs/macros.md §6).
 */
class MacroMidiOutletModule : public ModuleBase {
public:
    MacroMidiOutletModule()
        : ModuleBase("Macro MIDI Out", 0, 0) {
        enableVisualBuffer(true);
    }

    ~MacroMidiOutletModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed())
            return;

        if (auto* vb = getVisualBuffer()) {
            const float activity = midiMessages.isEmpty() ? 0.0f : 1.0f;
            for (int i = 0; i < buffer.getNumSamples(); ++i)
                vb->pushSample(activity);
        }
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }

    ModuleType getModuleType() const override { return ModuleType::MacroMidiOutlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroMidiOutletModule)
};
