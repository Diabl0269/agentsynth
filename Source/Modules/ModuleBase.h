#pragma once

#include "VisualBuffer.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>
#include <vector>

struct ModulationTarget {
    juce::String name;
    int channelIndex;
};

enum class ModulationCategory { Envelope, LFO, Oscillator, Sequencer, Filter, FX, Other };

enum class PortRole { Audio, ModCV, Pitch, Gate, Midi, Other };

struct LogicalPort {
    int visibleJackIndex =
        0; // which visible jack (0..getVisible*PortCount()-1) a wire to this raw channel should anchor to
    PortRole role = PortRole::Other;
    bool isPolyGroupHead = false; // true only for the lowest raw channel of a poly fan
    int polyVoiceSpan = 1;        // 1 = mono; N = head of an N-voice fan
};

enum class ModuleType {
    Oscillator,
    Filter,
    VCA,
    ADSR,
    LFO,
    Sequencer,
    PolySequencer,
    MidiKeyboard,
    PolyMidi,
    ExternalMidi,
    Attenuverter,
    Delay,
    Distortion,
    Reverb,
    Chorus,
    Phaser,
    Compressor,
    Flanger,
    Limiter,
    VoiceMixer,
    Noise
};

class ModuleBase : public juce::AudioProcessor {
public:
    ModuleBase(const juce::String& name, int numInputs, int numOutputs)
        : AudioProcessor(
              BusesProperties()
                  .withInput("Input", juce::AudioChannelSet::discreteChannels(std::max(1, numInputs)), numInputs > 0)
                  .withOutput("Output", juce::AudioChannelSet::discreteChannels(std::max(1, numOutputs)),
                              numOutputs > 0))
        , moduleName(name) {
        addParameter(bypassedParam = new juce::AudioParameterBool("bypassed", "Bypassed", false));
    }

    void addMuteParameter() {
        if (!mutedParam)
            addParameter(mutedParam = new juce::AudioParameterBool("muted", "Muted", false));
    }

    // Opt-in output-level stage for modules whose output is audio.
    //
    // Deliberately NOT added in the ModuleBase ctor: parameter position is load-bearing
    // for a few positional getParameters()[n] call sites, and "gain" is meaningless (or
    // actively wrong) on pitch/gate CV outputs — scaling a V/oct pitch CV detunes, and
    // scaling a gate drops it under the > 0.5f trigger threshold. Each module opts in,
    // and MUST call this AFTER its own addParameter() calls so the new parameter lands
    // last and existing positional lookups keep resolving to the same parameter.
    void addOutputLevelParameter(float defaultValue = 1.0f) {
        if (outputLevelParam)
            return;
        addParameter(outputLevelParam =
                         new juce::AudioParameterFloat("outputLevel", "Level", 0.0f, 1.0f, defaultValue));
        // Safe default if prepareToPlay (and therefore prepareOutputLevel) never runs:
        // snap-to-target, so applyOutputLevel takes its steady-state path at unity
        // instead of ramping up from a default-constructed 0 and silencing the module.
        smoothedOutputLevel.reset(1);
        smoothedOutputLevel.setCurrentAndTargetValue(defaultValue);
    }

    bool hasOutputLevel() const { return outputLevelParam != nullptr; }
    float getOutputLevel() const { return outputLevelParam != nullptr ? outputLevelParam->get() : 1.0f; }

    ~ModuleBase() override = default;

    const juce::String getName() const override { return moduleName; }

    bool isBypassed() const { return bypassedParam->get(); }
    void setBypassed(bool b) { bypassedParam->setValueNotifyingHost(b ? 1.0f : 0.0f); }

    bool isMuted() const { return mutedParam->get(); }
    void setMuted(bool m) { mutedParam->setValueNotifyingHost(m ? 1.0f : 0.0f); }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override = 0;
    void releaseResources() override {}
    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override = 0;

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; } // To be implemented later

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 0.0; }

    // Boilerplate
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int index) override { juce::ignoreUnused(index); }
    const juce::String getProgramName(int index) override {
        juce::ignoreUnused(index);
        return {};
    }
    void changeProgramName(int index, const juce::String& newName) override { juce::ignoreUnused(index, newName); }
    void getStateInformation(juce::MemoryBlock& destData) override {
        juce::ValueTree state("ModuleState");
        for (auto* param : getParameters()) {
            if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                state.setProperty(p->paramID, p->getValue(), nullptr);
            }
        }
        copyXmlToBinary(*state.createXml(), destData);
    }

    void setStateInformation(const void* data, int sizeInBytes) override {
        auto xmlState = getXmlFromBinary(data, sizeInBytes);
        if (xmlState != nullptr) {
            if (xmlState->hasTagName("ModuleState")) {
                juce::ValueTree state = juce::ValueTree::fromXml(*xmlState);
                for (auto* param : getParameters()) {
                    if (auto* p = dynamic_cast<juce::AudioProcessorParameterWithID*>(param)) {
                        if (state.hasProperty(p->paramID)) {
                            p->setValue((float)state.getProperty(p->paramID));
                        }
                    }
                }
            }
        }
    }

    void setModuleName(const juce::String& name) { moduleName = name; }

    virtual std::vector<ModulationTarget> getModulationTargets() const { return {}; }
    virtual juce::String getInputPortLabel(int channelIndex) const { return "In " + juce::String(channelIndex); }
    virtual juce::String getOutputPortLabel(int channelIndex) const { return "Out " + juce::String(channelIndex); }
    virtual int getVisibleInputPortCount() const { return getTotalNumInputChannels(); }
    virtual int getVisibleOutputPortCount() const { return getTotalNumOutputChannels(); }
    virtual ModulationCategory getModulationCategory() const { return ModulationCategory::Other; }
    virtual ModuleType getModuleType() const = 0;

    virtual LogicalPort mapInputChannel(int rawChannel) const {
        LogicalPort p;
        int vis = getVisibleInputPortCount();
        p.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, rawChannel) : 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = (rawChannel < vis);
        p.polyVoiceSpan = 1;
        return p;
    }

    virtual LogicalPort mapOutputChannel(int rawChannel) const {
        LogicalPort p;
        int vis = getVisibleOutputPortCount();
        p.visibleJackIndex = (vis > 0) ? juce::jlimit(0, vis - 1, rawChannel) : 0;
        p.role = PortRole::Other;
        p.isPolyGroupHead = (rawChannel < vis);
        p.polyVoiceSpan = 1;
        return p;
    }

    // Decouples display-advertising from JSON auto-promotion (used by AIStateMapper in a LATER increment; defined now).
    // Default: a channel is auto-promotable iff it is one of this module's getModulationTargets() channelIndex values.
    virtual bool isAutoPromotableModTarget(int dstChannel) const {
        for (const auto& t : getModulationTargets())
            if (t.channelIndex == dstChannel)
                return true;
        return false;
    }

    VisualBuffer* getVisualBuffer() { return visualBuffer.get(); }
    void enableVisualBuffer(bool enable) {
        if (enable && !visualBuffer)
            visualBuffer = std::make_unique<VisualBuffer>();
        else if (!enable)
            visualBuffer = nullptr;
    }

protected:
    // Call from prepareToPlay() when the module uses addOutputLevelParameter().
    void prepareOutputLevel(double sampleRate) {
        smoothedOutputLevel.reset(sampleRate, 0.01); // 10 ms ramp — anti-click on knob moves
        smoothedOutputLevel.setCurrentAndTargetValue(getOutputLevel());
    }

    // Scales the first numAudioChannels channels by the smoothed output level.
    //
    // Call at the END of the normal processBlock path only. Never on the bypass branch
    // (dry pass-through must stay untouched) and never on mute (already cleared) — see
    // the bypass/mute contract in docs/architecture.md. No-op when the module did not
    // call addOutputLevelParameter().
    void applyOutputLevel(juce::AudioBuffer<float>& buffer, int numAudioChannels) {
        if (outputLevelParam == nullptr)
            return;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = juce::jmin(numAudioChannels, buffer.getNumChannels());
        if (numSamples == 0 || numChannels <= 0)
            return;

        smoothedOutputLevel.setTargetValue(outputLevelParam->get());

        if (!smoothedOutputLevel.isSmoothing()) {
            const float gain = smoothedOutputLevel.getCurrentValue();
            if (gain != 1.0f)
                for (int ch = 0; ch < numChannels; ++ch)
                    buffer.applyGain(ch, 0, numSamples, gain);
            return;
        }

        // getArrayOfWritePointers() avoids re-resolving the pointer per sample and
        // allocates nothing — the per-sample walk keeps the ramp exact even when it
        // completes mid-block.
        auto* const* channels = buffer.getArrayOfWritePointers();
        for (int i = 0; i < numSamples; ++i) {
            const float gain = smoothedOutputLevel.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= gain;
        }
    }

    juce::AudioParameterBool* bypassedParam = nullptr;
    juce::AudioParameterBool* mutedParam = nullptr;
    juce::AudioParameterFloat* outputLevelParam = nullptr;

private:
    juce::String moduleName;
    std::unique_ptr<VisualBuffer> visualBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputLevel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBase)
};

// Look a parameter up by paramID instead of by position. Parameter order is not part of
// a module's contract — adding one (e.g. addOutputLevelParameter) silently repoints any
// getParameters()[n] index. Returns nullptr when the processor has no such parameter.
inline juce::RangedAudioParameter* findParameterByID(juce::AudioProcessor* processor, const juce::String& paramID) {
    if (processor == nullptr)
        return nullptr;
    for (auto* param : processor->getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*>(param))
            if (ranged->paramID == paramID)
                return ranged;
    return nullptr;
}
