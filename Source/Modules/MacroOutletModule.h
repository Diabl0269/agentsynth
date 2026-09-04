#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Macro Out" — an audio/CV outlet jack on a Macro's boundary (P8-15 Macro I/O,
 * docs/macros.md §5).
 *
 * The exact mirror of MacroInletModule in the other direction — see that class's comment for the
 * full reasoning (pass-through semantics, the declare-max/vary-visible channel-shape mechanism,
 * why there is no "muted" parameter). Kept as a separate class rather than a constructor flag on
 * one, matching the one-class-per-ModuleType shape every other module in this codebase uses.
 *
 * INTERNAL-ONLY, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module
 * library, not offered by the replace menu, never authorable by a model
 * (kNonAuthorableModuleTypes, docs/macros.md §6).
 */
class MacroOutletModule : public ModuleBase {
public:
    /** See MacroInletModule::kMaxChannels — identical reasoning, mirrored. */
    static constexpr int kMaxChannels = 8;

    MacroOutletModule()
        : ModuleBase("Macro Out", kMaxChannels, kMaxChannels) {
        enableVisualBuffer(true);
    }

    ~MacroOutletModule() override = default;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(sampleRate, samplesPerBlock);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        const int numSamples = buffer.getNumSamples();
        const int visible = getVisibleOutputPortCount();

        for (int ch = visible; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);

        if (isBypassed())
            return;

        if (auto* vb = getVisualBuffer())
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(visible > 0 ? buffer.getReadPointer(0)[i] : 0.0f);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::MacroOutlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return visibleChannels_.load(std::memory_order_relaxed); }
    int getVisibleOutputPortCount() const override { return visibleChannels_.load(std::memory_order_relaxed); }

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("channels", visibleChannels_.load(std::memory_order_relaxed));
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject())
            if (obj->hasProperty("channels"))
                visibleChannels_.store(juce::jlimit(1, kMaxChannels, (int)obj->getProperty("channels")),
                                       std::memory_order_relaxed);
    }

private:
    std::atomic<int> visibleChannels_{1}; // Mono default — see MacroInletModule

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroOutletModule)
};
