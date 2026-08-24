#pragma once

#include "../ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

// Diode-ring modulator after Parker, "A Simple Digital Model of the Diode-Based
// Ring Modulator" (DAFx-11). Clean four-quadrant multiply lives on Math's Mult
// output; this module is the nonlinear, oversampled, mixable audio effect.
class RingModulatorModule
    : public ModuleBase
    , public juce::AudioProcessorParameter::Listener {
public:
    RingModulatorModule()
        : ModuleBase("Ring Modulator", 5, 2) // Carrier, Modulator, Mix CV, Drive CV, Character CV
    {
        addParameter(driveParam = new juce::AudioParameterFloat("drive", "Drive", 0.5f, 8.0f, 1.0f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 1.0f));
        addParameter(characterParam = new juce::AudioParameterFloat("character", "Character", 0.0f, 1.0f, 0.5f));
        addParameter(oversamplingParam = new juce::AudioParameterChoice("oversampling", "Oversampling",
                                                                        juce::StringArray{"Off", "2x", "4x"}, 1));
        // OUTPUT-side Dual I/O, the Voice Mixer's shape rather than the usual FX one: raw ch0/ch1 on
        // the INPUT side are Carrier and Modulator — two unrelated mono jacks, not a stereo pair —
        // so only the output pair collapses. Added before Level, like every other FX (docs/fx_modules.md).
        addDualIOParameter();
        addOutputLevelParameter();
        addMuteParameter();
        oversamplingParam->addListener(this);
        enableVisualBuffer(true);
    }

    ~RingModulatorModule() override {
        if (oversamplingParam != nullptr)
            oversamplingParam->removeListener(this);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        oversamplers[0] = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true); // 2x
        oversamplers[1] = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true); // 4x

        for (auto& os : oversamplers)
            os->initProcessing(static_cast<size_t>(samplesPerBlock));

        dryBuffer.setSize(2, samplesPerBlock);

        smoothedDrive.reset(sampleRate, 0.005);
        smoothedMix.reset(sampleRate, 0.005);
        smoothedCharacter.reset(sampleRate, 0.005);
        smoothedDrive.setCurrentAndTargetValue(*driveParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        smoothedCharacter.setCurrentAndTargetValue(*characterParam);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
        spec.numChannels = 2;
        latencyDelay.prepare(spec);
        latencyDelay.setDelay(static_cast<float>(getLatencyInSamples()));
        latencyDelay.reset();

        if (oversamplers[0])
            oversamplers[0]->reset();
        if (oversamplers[1])
            oversamplers[1]->reset();

        setLatencySamples(juce::roundToInt(getLatencyInSamples()));

        prepareOutputLevel(sampleRate);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        int numSamples = buffer.getNumSamples();
        int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        if (numChannels < 2 || dryBuffer.getNumSamples() < numSamples) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        const float* cvMix = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvDrive = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;
        const float* cvCharacter = (numChannels > 4) ? buffer.getReadPointer(4) : nullptr;

        bool cvMixActive = isChannelActive(cvMix, numSamples);
        bool cvDriveActive = isChannelActive(cvDrive, numSamples);
        bool cvCharacterActive = isChannelActive(cvCharacter, numSamples);

        smoothedDrive.setTargetValue(*driveParam);
        smoothedMix.setTargetValue(*mixParam);
        smoothedCharacter.setTargetValue(*characterParam);

        // Dry is the carrier, duplicated to both output channels so mix=0 is
        // a mono-to-stereo pass-through of the unprocessed carrier.
        dryBuffer.copyFrom(0, 0, buffer.getReadPointer(0), numSamples);
        dryBuffer.copyFrom(1, 0, buffer.getReadPointer(0), numSamples);

        juce::dsp::AudioBlock<float> fullDryBlock(dryBuffer);
        juce::dsp::AudioBlock<float> dryBlock = fullDryBlock.getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
        latencyDelay.process(dryContext);

        int oversamplingIndex = oversamplingParam ? oversamplingParam->getIndex() : 0;

        if (oversamplingIndex == 0) {
            float* carrier = buffer.getWritePointer(0);
            float* modulator = buffer.getWritePointer(1);
            for (int i = 0; i < numSamples; ++i) {
                float driveMod = cvDriveActive ? cvDrive[i] : 0.0f;
                float drive = juce::jlimit(0.5f, 8.0f, smoothedDrive.getNextValue() + driveMod * 4.0f);
                float characterMod = cvCharacterActive ? cvCharacter[i] : 0.0f;
                float vb = 0.0f, vl = 0.0f;
                characterToBreakpoints(juce::jlimit(0.0f, 1.0f, smoothedCharacter.getNextValue() + characterMod), vb,
                                       vl);
                const float wet = diodeRing(carrier[i] * drive, modulator[i] * drive, vb, vl);
                carrier[i] = wet;
                modulator[i] = wet;
            }
        } else {
            auto* os = oversamplers[oversamplingIndex - 1].get();
            if (os) {
                int factor = static_cast<int>(os->getOversamplingFactor());

                juce::dsp::AudioBlock<float> fullBlock(buffer);
                juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
                auto oversampledBlock = os->processSamplesUp(audioBlock);

                auto* carrier = oversampledBlock.getChannelPointer(0);
                auto* modulator = oversampledBlock.getChannelPointer(1);
                float currentDrive = 1.0f;
                float currentVb = 0.2f;
                float currentVl = 0.4f;
                for (size_t i = 0; i < oversampledBlock.getNumSamples(); ++i) {
                    int sampleIdx = static_cast<int>(i) / factor;
                    if (i % static_cast<size_t>(factor) == 0) {
                        const int src = std::min(sampleIdx, numSamples - 1);
                        float driveMod = cvDriveActive ? cvDrive[src] : 0.0f;
                        currentDrive = juce::jlimit(0.5f, 8.0f, smoothedDrive.getNextValue() + driveMod * 4.0f);
                        float characterMod = cvCharacterActive ? cvCharacter[src] : 0.0f;
                        characterToBreakpoints(
                            juce::jlimit(0.0f, 1.0f, smoothedCharacter.getNextValue() + characterMod), currentVb,
                            currentVl);
                    }
                    const float wet =
                        diodeRing(carrier[i] * currentDrive, modulator[i] * currentDrive, currentVb, currentVl);
                    carrier[i] = wet;
                    modulator[i] = wet;
                }

                os->processSamplesDown(audioBlock);
            }
        }

        float* wetPtrs[2] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
        const float* dryPtrs[2] = {dryBuffer.getReadPointer(0), dryBuffer.getReadPointer(1)};

        for (int i = 0; i < numSamples; ++i) {
            float mixMod = cvMixActive ? cvMix[i] : 0.0f;
            float mix = juce::jlimit(0.0f, 1.0f, smoothedMix.getNextValue() + mixMod);
            if (mix < 0.001f)
                mix = 0.0f;

            for (int ch = 0; ch < 2; ++ch) {
                float wet = wetPtrs[ch][i];
                float dry = dryPtrs[ch][i];
                wetPtrs[ch][i] = dry + (wet - dry) * mix;
            }
        }

        applyOutputLevel(buffer, 2);

        if (auto* vb = getVisualBuffer()) {
            const float* ch0 = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(ch0[i]);
        }

        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Carrier", "Modulator", "Mix", "Drive", "Character"};
        return (i >= 0 && i < 5) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    // Output-only Dual I/O: "Left"/"Right" when split, one "Audio" jack owning raw ch0+ch1 when
    // collapsed (both legs carry the same wet signal, so collapsing loses nothing audible).
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // The INPUT side is deliberately NOT mapped through mapStereoPairInput: ch1 is the Modulator
    // jack, and calling it PortRole::Audio would make ModuleBase::rightAudioLegChannel()'s ch1 look
    // like an input right leg — GraphEditor's Dual I/O toggle would then wire a neighbour's Audio R
    // into this module's Modulator input. The inherited map leaves those five jacks as they were.

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Mix", 2}, {"Drive", 3}, {"Character", 4}};
    }

    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::RingModulator; }

    double getLatencyInSamples() const {
        if (!oversamplingParam)
            return 0.0;
        int idx = oversamplingParam->getIndex();
        if (idx == 0)
            return 0.0;
        return oversamplers[idx - 1] ? oversamplers[idx - 1]->getLatencyInSamples() : 0.0;
    }

    void parameterValueChanged(int parameterIndex, float newValue) override {
        juce::ignoreUnused(newValue);
        if (oversamplingParam && parameterIndex == oversamplingParam->getParameterIndex()) {
            float lat = static_cast<float>(getLatencyInSamples());
            setLatencySamples(juce::roundToInt(lat));
            latencyDelay.setDelay(lat);
        }
    }

    void parameterGestureChanged(int parameterIndex, bool gestureIsStarting) override {
        juce::ignoreUnused(parameterIndex, gestureIsStarting);
    }

private:
    // Parker piecewise-quadratic diode: zero below vb, quadratic to vl, linear above.
    static float diode(float v, float vb, float vl, float h) {
        if (v <= vb)
            return 0.0f;
        const float denom = 2.0f * (vl - vb);
        if (denom <= 1.0e-8f)
            return v > vb ? h * (v - vb) : 0.0f;
        if (v <= vl)
            return h * (v - vb) * (v - vb) / denom;
        return h * v - h * vl + h * (vl - vb) * (vl - vb) / denom;
    }

    static float diodeRing(float carrier, float modulator, float vb, float vl) {
        constexpr float h = 1.0f;
        const float c2 = 0.5f * carrier;
        const float m = modulator;
        return diode(m + c2, vb, vl, h) + diode(-m + c2, vb, vl, h) - diode(m - c2, vb, vl, h) -
               diode(-m - c2, vb, vl, h);
    }

    // Character 0 ≈ near-ideal diodes (close to a multiply); 1 ≈ large forward-bias
    // dead-zone (gated, dissonant). Midpoint is Parker's typical vb≈0.2 / vl≈0.4.
    static void characterToBreakpoints(float character, float& vb, float& vl) {
        character = juce::jlimit(0.0f, 1.0f, character);
        vb = juce::jmap(character, 0.02f, 0.5f);
        vl = juce::jmap(character, 0.05f, 1.0f);
    }

    static bool isChannelActive(const float* cv, int numSamples) {
        if (cv == nullptr)
            return false;
        float rms = 0.0f;
        for (int i = 0; i < numSamples; ++i)
            rms += cv[i] * cv[i];
        return (rms / static_cast<float>(numSamples)) > 1.0e-3f;
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[2]; // [0]=2x, [1]=4x
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedCharacter;
    juce::dsp::DelayLine<float> latencyDelay{4096};

    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterFloat* characterParam = nullptr;
    juce::AudioParameterChoice* oversamplingParam = nullptr;
};
