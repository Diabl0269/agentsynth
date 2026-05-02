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
    VoiceMixer
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

    ~ModuleBase() override = default;

    const juce::String getName() const override { return moduleName; }

    bool isBypassed() const { return bypassedParam->get(); }
    void setBypassed(bool b) { bypassedParam->setValueNotifyingHost(b ? 1.0f : 0.0f); }

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

    VisualBuffer* getVisualBuffer() { return visualBuffer.get(); }
    void enableVisualBuffer(bool enable) {
        if (enable && !visualBuffer)
            visualBuffer = std::make_unique<VisualBuffer>();
        else if (!enable)
            visualBuffer = nullptr;
    }

protected:
    juce::AudioParameterBool* bypassedParam = nullptr;

private:
    juce::String moduleName;
    std::unique_ptr<VisualBuffer> visualBuffer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ModuleBase)
};
