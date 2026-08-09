#pragma once

#include "ModuleBase.h"
#include <array>
#include <juce_core/juce_core.h>

/**
 * Macro Control — a bank of assignable CV knobs.
 *
 * Each macro emits a steady CV on its own output jack. A single jack can feed any number of
 * destinations: the graph wraps every CV cable in its own Attenuverter, so one knob sweeps many
 * parameters at once with independent depth and polarity per destination. That is the whole macro
 * idea — the bank does not need N-to-M routing of its own, the smart cables already provide it.
 *
 * Channel layout: output channel i carries macro i+1. All kMaxMacros channels exist for the
 * lifetime of the node — JUCE fixes the bus layout at construction, and rebuilding it would drop
 * every existing graph connection — so the visible bank size is a *parameter* (`macroCount`).
 * Channels at or above it are cleared every block and hidden by getVisibleOutputPortCount().
 */
class MacroControlModule : public ModuleBase {
public:
    static constexpr int kMaxMacros = 16;
    static constexpr int kMinMacros = 1;
    static constexpr int kDefaultMacros = 8;

    MacroControlModule()
        : ModuleBase("Macros", 0, kMaxMacros) // no inputs; one CV output per macro
    {
        addParameter(countParam =
                         new juce::AudioParameterInt("macroCount", "Knobs", kMinMacros, kMaxMacros, kDefaultMacros));
        addParameter(bipolarParam = new juce::AudioParameterBool("macroBipolar", "Bipolar", false));

        for (int i = 0; i < kMaxMacros; ++i)
            addParameter(macroParams[(size_t)i] = new juce::AudioParameterFloat("macro" + juce::String(i + 1),
                                                                                macroName(i), 0.0f, 1.0f, 0.0f));

        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        for (int i = 0; i < kMaxMacros; ++i) {
            smoothed[(size_t)i].reset(currentSampleRate, 0.02);
            smoothed[(size_t)i].setCurrentAndTargetValue(outputFor(i));
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        // Pure source module: there is no audio input, so bypass has no dry signal to pass
        // through and clears like mute (same exception as Oscillator / LFO / Noise).
        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        const int active = juce::jmin(countParam->get(), numChannels);

        for (int i = 0; i < active; ++i) {
            smoothed[(size_t)i].setTargetValue(outputFor(i));
            auto* out = buffer.getWritePointer(i);
            for (int s = 0; s < numSamples; ++s)
                out[s] = smoothed[(size_t)i].getNextValue();
        }

        // Keep hidden macros tracking their knob so re-enabling them does not ramp from a stale
        // value, then silence their channels.
        for (int i = active; i < kMaxMacros; ++i)
            smoothed[(size_t)i].setCurrentAndTargetValue(outputFor(i));

        for (int ch = active; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    //==============================================================================
    // Port model
    //==============================================================================

    int getVisibleInputPortCount() const override { return 0; }
    int getVisibleOutputPortCount() const override { return countParam->get(); }

    juce::String getOutputPortLabel(int i) const override {
        return (i >= 0 && i < kMaxMacros) ? macroName(i) : ModuleBase::getOutputPortLabel(i);
    }

    LogicalPort mapOutputChannel(int rawChannel) const override {
        LogicalPort p;
        const int visible = getVisibleOutputPortCount();
        p.visibleJackIndex = (visible > 0) ? juce::jlimit(0, visible - 1, rawChannel) : 0;
        p.role = PortRole::ModCV;
        p.isPolyGroupHead = (rawChannel < visible);
        p.polyVoiceSpan = 1;
        return p;
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }
    ModuleType getModuleType() const override { return ModuleType::MacroControl; }

    //==============================================================================
    // UI accessors
    //==============================================================================

    /** Number of macros currently exposed as knobs and output jacks. */
    int getMacroCount() const { return countParam->get(); }

    /** Raw knob position (always 0..1, regardless of bipolar). */
    float getMacroKnob(int index) const {
        return (index >= 0 && index < kMaxMacros) ? macroParams[(size_t)index]->get() : 0.0f;
    }

    /** The CV this macro emits — 0..1 unipolar, -1..1 when the bank is bipolar. */
    float getMacroOutput(int index) const { return (index >= 0 && index < kMaxMacros) ? outputFor(index) : 0.0f; }

    bool isBipolar() const { return bipolarParam->get(); }

    /** Display name of macro `index` ("M1" … "M16"); also the slider ComponentID used by the UI. */
    static juce::String macroName(int index) { return "M" + juce::String(index + 1); }

private:
    float outputFor(int index) const {
        const float knob = macroParams[(size_t)index]->get();
        return bipolarParam->get() ? (knob * 2.0f - 1.0f) : knob;
    }

    juce::AudioParameterInt* countParam = nullptr;
    juce::AudioParameterBool* bipolarParam = nullptr;
    std::array<juce::AudioParameterFloat*, kMaxMacros> macroParams{};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kMaxMacros> smoothed{};

    double currentSampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroControlModule)
};
