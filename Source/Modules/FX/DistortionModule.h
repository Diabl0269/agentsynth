#pragma once

#include "../ModuleBase.h"
#include <juce_dsp/juce_dsp.h>

class DistortionModule
    : public ModuleBase
    , public juce::AudioProcessorParameter::Listener {
public:
    DistortionModule()
        : ModuleBase("Distortion", 4, 2) // 2 Audio + 2 CV (Drive, Mix)
    {
        addParameter(driveParam = new juce::AudioParameterFloat("drive", "Drive", 1.0f, 20.0f, 1.0f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 0.5f));
        addParameter(typeParam = new juce::AudioParameterChoice("type", "Type",
                                                                juce::StringArray{"Soft", "Hard", "Foldback"}, 0));
        addParameter(oversamplingParam = new juce::AudioParameterChoice("oversampling", "Oversampling",
                                                                        juce::StringArray{"Off", "2x", "4x"},
                                                                        1)); // default 2x preserves backward compat
        addDualIOParameter();
        addOutputLevelParameter();
        addMuteParameter();
        oversamplingParam->addListener(this);
        enableVisualBuffer(true);
    }

    ~DistortionModule() override {
        if (oversamplingParam != nullptr)
            oversamplingParam->removeListener(this);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        // Pre-allocate 2x and 4x oversamplers for real-time safe switching
        oversamplers[0] = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 1, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true); // 2x
        oversamplers[1] = std::make_unique<juce::dsp::Oversampling<float>>(
            2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true); // 4x

        for (auto& os : oversamplers)
            os->initProcessing(static_cast<size_t>(samplesPerBlock));

        dryBuffer.setSize(2, samplesPerBlock);

        smoothedDrive.reset(sampleRate, 0.005);
        smoothedMix.reset(sampleRate, 0.005);
        smoothedDrive.setCurrentAndTargetValue(*driveParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);

        smoothedMakeupGain.reset(sampleRate, 0.05); // 50ms smoothing
        smoothedMakeupGain.setCurrentAndTargetValue(1.0f);

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = samplesPerBlock;
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

        // Safety: ensure we have at least stereo and buffers are prepared
        if (numSamples == 0 || numChannels < 2 || dryBuffer.getNumSamples() < numSamples) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        const float* cvDrive = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvMix = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;

        bool cvDriveActive = false;
        bool cvMixActive = false;
        if (cvDrive) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvDrive[i] * cvDrive[i];
            cvDriveActive = (rms / numSamples) > 1e-3f;
        }
        if (cvMix) {
            float rms = 0.0f;
            for (int i = 0; i < numSamples; ++i)
                rms += cvMix[i] * cvMix[i];
            cvMixActive = (rms / numSamples) > 1e-3f;
        }

        smoothedDrive.setTargetValue(*driveParam);
        smoothedMix.setTargetValue(*mixParam);

        // Save dry signal (dryBuffer pre-allocated in prepareToPlay)
        for (int ch = 0; ch < 2; ++ch)
            dryBuffer.copyFrom(ch, 0, buffer.getReadPointer(ch), numSamples);

        // Delay the dry signal to align perfectly with the oversampled wet signal
        juce::dsp::AudioBlock<float> fullDryBlock(dryBuffer);
        juce::dsp::AudioBlock<float> dryBlock = fullDryBlock.getSubBlock(0, (size_t)numSamples);
        juce::dsp::ProcessContextReplacing<float> dryContext(dryBlock);
        latencyDelay.process(dryContext);

        int oversamplingIndex = oversamplingParam ? oversamplingParam->getIndex() : 0;
        int type = typeParam ? typeParam->getIndex() : 0;

        if (oversamplingIndex == 0) {
            // 1x rate (oversampling Off)
            for (int ch = 0; ch < 2; ++ch) {
                float* data = buffer.getWritePointer(ch);
                for (int i = 0; i < numSamples; ++i) {
                    float driveMod = cvDriveActive ? cvDrive[i] : 0.0f;
                    float drive = juce::jlimit(1.0f, 20.0f, smoothedDrive.getNextValue() + (driveMod * 10.0f));
                    data[i] = applyWaveshaper(data[i], drive, type);
                }
            }
        } else {
            // 2x or 4x: upsample, distort, downsample
            auto* os = oversamplers[oversamplingIndex - 1].get();
            if (os) {
                int factor = static_cast<int>(os->getOversamplingFactor());

                juce::dsp::AudioBlock<float> fullBlock(buffer);
                juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
                auto oversampledBlock = os->processSamplesUp(audioBlock);

                for (size_t ch = 0; ch < oversampledBlock.getNumChannels(); ++ch) {
                    auto* data = oversampledBlock.getChannelPointer(ch);
                    float currentDrive = 1.0f;
                    for (size_t i = 0; i < oversampledBlock.getNumSamples(); ++i) {
                        int sampleIdx = static_cast<int>(i) / factor;
                        // Advance smoother once per original-rate sample, hold for sub-samples
                        if (i % static_cast<size_t>(factor) == 0) {
                            float driveMod = cvDriveActive ? cvDrive[std::min(sampleIdx, numSamples - 1)] : 0.0f;
                            currentDrive = juce::jlimit(1.0f, 20.0f, smoothedDrive.getNextValue() + (driveMod * 10.0f));
                        }
                        data[i] = applyWaveshaper(data[i], currentDrive, type);
                    }
                }

                os->processSamplesDown(audioBlock);
            }
        }
        // Compute RMS for dynamic makeup gain
        float sumWetSq = 0.0f;
        float sumDrySq = 0.0f;
        for (int ch = 0; ch < 2; ++ch) {
            auto* w = buffer.getReadPointer(ch);
            auto* d = dryBuffer.getReadPointer(ch);
            for (int i = 0; i < numSamples; ++i) {
                sumWetSq += w[i] * w[i];
                sumDrySq += d[i] * d[i];
            }
        }

        float rmsWet = std::sqrt(sumWetSq / (numSamples * 2.0f));
        float rmsDry = std::sqrt(sumDrySq / (numSamples * 2.0f));
        float targetMakeup = (rmsWet > 1e-4f) ? (rmsDry / rmsWet) : 1.0f;
        if (std::isnan(targetMakeup) || std::isinf(targetMakeup))
            targetMakeup = 1.0f;
        targetMakeup = juce::jlimit(0.01f, 1.0f, targetMakeup);
        smoothedMakeupGain.setTargetValue(targetMakeup);

        // Wet/dry blend at 1x rate
        float* wetPtrs[2] = {buffer.getWritePointer(0), buffer.getWritePointer(1)};
        const float* dryPtrs[2] = {dryBuffer.getReadPointer(0), dryBuffer.getReadPointer(1)};

        for (int i = 0; i < numSamples; ++i) {
            float makeup = smoothedMakeupGain.getNextValue();
            float mixMod = cvMixActive ? cvMix[i] : 0.0f;
            float rawMix = juce::jlimit(0.0f, 1.0f, smoothedMix.getNextValue() + mixMod);

            // Linear mix for more predictable control, but still with deadzone for transparency
            float mix = rawMix;
            if (mix < 0.001f)
                mix = 0.0f;

            for (int ch = 0; ch < 2; ++ch) {
                float wet = wetPtrs[ch][i] * makeup;
                float dry = dryPtrs[ch][i];
                wetPtrs[ch][i] = dry + (wet - dry) * mix;
            }
        }

        // Applied before the scope push so the visualiser shows the actual module output.
        applyOutputLevel(buffer, 2);

        // Push to scope
        if (auto* vb = getVisualBuffer()) {
            const float* ch0 = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(ch0[i]);
            }
        }

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Drive", "Mix"};
        return stereoInputLabel(i, 2, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(2); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 2); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Drive", 2}, {"Mix", 3}}; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Distortion; }

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
    static float applyWaveshaper(float input, float drive, int type) {
        // Soft clipper
        if (type == 0) {
            float k = drive - 1.0f;
            if (k <= 0.0f)
                return input;
            return input * (1.0f + k) / (1.0f + k * std::abs(input));
        }
        // Hard clipper
        if (type == 1) {
            float threshold = 1.0f / (1.0f + (drive - 1.0f) * 0.1f);
            return juce::jlimit(-threshold, threshold, input * drive) / threshold;
        }
        // Foldback
        if (type == 2) {
            float x = input * drive;
            float threshold = 0.8f;
            if (x > threshold)
                return 2.0f * threshold - x;
            if (x < -threshold)
                return -2.0f * threshold - x;
            return x;
        }
        return input;
    }

    std::unique_ptr<juce::dsp::Oversampling<float>> oversamplers[2]; // [0]=2x, [1]=4x
    juce::AudioBuffer<float> dryBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDrive;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMakeupGain;
    juce::dsp::DelayLine<float> latencyDelay{4096};

    juce::AudioParameterFloat* driveParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
    juce::AudioParameterChoice* typeParam = nullptr;
    juce::AudioParameterChoice* oversamplingParam = nullptr;
};
