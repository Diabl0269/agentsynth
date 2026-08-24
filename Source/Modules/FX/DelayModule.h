#pragma once

#include "../ModuleBase.h"

class DelayModule : public ModuleBase {
public:
    DelayModule()
        : ModuleBase("Delay", 2, 2) {
        addParameter(timeParam = new juce::AudioParameterFloat("time", "Time (ms)", 1.0f, 1000.0f, 250.0f));
        addParameter(feedbackParam = new juce::AudioParameterFloat("feedback", "Feedback", 0.0f, 0.95f, 0.5f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 0.3f));
        addDualIOParameter();
        addOutputLevelParameter();
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        this->sampleRate = sampleRate;
        int maxDelaySamples = (int)(1.0f * sampleRate); // 1 second
        delayBuffer.setSize(2, maxDelaySamples + samplesPerBlock);
        delayBuffer.clear();
        writePos = 0;

        smoothedTime.reset(sampleRate, 0.05);      // 50ms ramp for time
        smoothedFeedback.reset(sampleRate, 0.005); // 5ms ramp
        smoothedMix.reset(sampleRate, 0.005);      // 5ms ramp

        smoothedTime.setCurrentAndTargetValue(*timeParam);
        smoothedFeedback.setCurrentAndTargetValue(*feedbackParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        prepareOutputLevel(sampleRate);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed()) {
            // Pure stereo module — no CV channels to clear; pass dry audio through unchanged
            return;
        }
        if (isMuted()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(midiMessages);

        smoothedTime.setTargetValue(*timeParam);
        smoothedFeedback.setTargetValue(*feedbackParam);
        smoothedMix.setTargetValue(*mixParam);

        int bufferSize = buffer.getNumSamples();
        int delayBufferSize = delayBuffer.getNumSamples();

        int numChannels = juce::jmin(buffer.getNumChannels(), delayBuffer.getNumChannels());
        int localWritePos = writePos;

        for (int i = 0; i < bufferSize; ++i) {
            float delayTimeMs = smoothedTime.getNextValue();
            float feedback = smoothedFeedback.getNextValue();
            float mix = smoothedMix.getNextValue();
            float delaySamplesF = delayTimeMs * 0.001f * static_cast<float>(sampleRate);

            for (int ch = 0; ch < numChannels; ++ch) {
                auto* data = buffer.getWritePointer(ch);
                auto* delayData = delayBuffer.getWritePointer(ch);

                float input = data[i];
                float readPosF =
                    static_cast<float>(localWritePos) - delaySamplesF + static_cast<float>(delayBufferSize);
                float delayedSample = linearInterpolate(delayData, delayBufferSize, readPosF);

                delayData[localWritePos] = input + (delayedSample * feedback);
                data[i] = (delayedSample * mix) + (input * (1.0f - mix));
            }

            localWritePos = (localWritePos + 1) % delayBufferSize;
        }

        writePos = localWritePos;

        // Output stage, deliberately outside the feedback path above — the delay line
        // stores the pre-level signal, so lowering Level does not starve the repeats.
        applyOutputLevel(buffer, 2);
    }

    juce::String getInputPortLabel(int i) const override { return stereoInputLabel(i, 0, nullptr); }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(0); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 0); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Time", 2}, {"Feedback", 3}, {"Mix", 4}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Delay; }

private:
    static float linearInterpolate(const float* buffer, int bufferSize, float fractionalPos) {
        while (fractionalPos < 0.0f)
            fractionalPos += static_cast<float>(bufferSize);

        int pos0 = static_cast<int>(fractionalPos) % bufferSize;
        int pos1 = (pos0 + 1) % bufferSize;
        float frac = fractionalPos - std::floor(fractionalPos);

        return buffer[pos0] * (1.0f - frac) + buffer[pos1] * frac;
    }

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    double sampleRate = 44100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTime;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;

    juce::AudioParameterFloat* timeParam;
    juce::AudioParameterFloat* feedbackParam;
    juce::AudioParameterFloat* mixParam;
};
