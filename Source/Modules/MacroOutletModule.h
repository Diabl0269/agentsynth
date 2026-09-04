#pragma once

#include "MacroPortShape.h"
#include "ModuleBase.h"
#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>

/**
 * @brief "Macro Out" — an audio/CV outlet jack on a Macro's boundary (P8-15 Macro I/O,
 * docs/macros.md §5).
 *
 * The exact mirror of MacroInletModule in the other direction — see that class's comment for the
 * full reasoning (pass-through semantics, the declare-max/vary-visible channel-shape mechanism,
 * the Stereo split-block leg, the Poly-N fanned jack, why there is no "muted" parameter). Kept as
 * a separate class rather than a constructor flag on one, matching the one-class-per-ModuleType
 * shape every other module in this codebase uses.
 *
 * INTERNAL-ONLY, the same three exclusions as Track In / Rec Tap / Track Audio: not in the module
 * library, not offered by the replace menu, never authorable by a model
 * (kNonAuthorableModuleTypes, docs/macros.md §6).
 */
class MacroOutletModule : public ModuleBase {
public:
    /** See MacroInletModule::kMaxChannels — identical reasoning, mirrored. */
    static constexpr int kMaxChannels = 8;
    /** See MacroInletModule::kRightBase — identical reasoning, mirrored. */
    static constexpr int kRightBase = 4;

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

        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            if (!isRawChannelActive(ch))
                buffer.clear(ch, 0, numSamples);

        if (isBypassed())
            return;

        if (auto* vb = getVisualBuffer())
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(buffer.getReadPointer(0)[i]);
    }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModuleType getModuleType() const override { return ModuleType::MacroOutlet; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Other; }

    int getVisibleInputPortCount() const override { return visibleJackCount(); }
    int getVisibleOutputPortCount() const override { return visibleJackCount(); }

    LogicalPort mapInputChannel(int rawChannel) const override { return mapChannelForShape(rawChannel); }
    LogicalPort mapOutputChannel(int rawChannel) const override { return mapChannelForShape(rawChannel); }

    int rightAudioLegChannel() const override {
        return shape_.load(std::memory_order_relaxed) == MacroPortShape::Stereo ? kRightBase : -1;
    }

    void setPortShape(MacroPortShape shape, int voiceCount = 1) {
        shape_.store(shape, std::memory_order_relaxed);
        voiceCount_.store(juce::jlimit(1, kMaxChannels, voiceCount), std::memory_order_relaxed);
    }
    MacroPortShape getPortShape() const { return shape_.load(std::memory_order_relaxed); }
    int getVoiceCount() const { return voiceCount_.load(std::memory_order_relaxed); }

    juce::var getExtraState() const override {
        auto* obj = new juce::DynamicObject();
        obj->setProperty("shape", macroPortShapeToString(shape_.load(std::memory_order_relaxed)));
        obj->setProperty("voices", voiceCount_.load(std::memory_order_relaxed));
        return juce::var(obj);
    }

    void setExtraState(const juce::var& state) override {
        if (auto* obj = state.getDynamicObject()) {
            if (obj->hasProperty("shape"))
                shape_.store(macroPortShapeFromString(obj->getProperty("shape").toString()), std::memory_order_relaxed);
            if (obj->hasProperty("voices"))
                voiceCount_.store(juce::jlimit(1, kMaxChannels, (int)obj->getProperty("voices")),
                                  std::memory_order_relaxed);
        }
    }

private:
    int visibleJackCount() const {
        switch (shape_.load(std::memory_order_relaxed)) {
        case MacroPortShape::Stereo:
            return 2;
        case MacroPortShape::Mono:
        case MacroPortShape::Poly:
        default:
            return 1;
        }
    }

    LogicalPort mapChannelForShape(int rawChannel) const {
        LogicalPort p;
        switch (shape_.load(std::memory_order_relaxed)) {
        case MacroPortShape::Stereo:
            if (rawChannel == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            } else if (rawChannel == kRightBase) {
                p.visibleJackIndex = 1;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            }
            break;
        case MacroPortShape::Poly: {
            const int voices = juce::jlimit(1, kMaxChannels, voiceCount_.load(std::memory_order_relaxed));
            if (rawChannel < voices) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = (rawChannel == 0);
                p.polyVoiceSpan = voices;
            }
            break;
        }
        case MacroPortShape::Mono:
        default:
            if (rawChannel == 0) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Audio;
                p.isPolyGroupHead = true;
            }
            break;
        }
        return p;
    }

    bool isRawChannelActive(int rawChannel) const { return mapChannelForShape(rawChannel).role == PortRole::Audio; }

    std::atomic<MacroPortShape> shape_{MacroPortShape::Mono};
    std::atomic<int> voiceCount_{1};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacroOutletModule)
};
