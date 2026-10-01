#pragma once

#include "../GainReductionMeterSource.h"
#include "../ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

// The limiter core is juce::dsp::Limiter's own algorithm (two juce::dsp::Compressor stages, a smoothed
// output volume and a hard clip at +-1), written out here only so the gain the stages take is
// visible to the gain-reduction meter; it renders sample for sample as juce::dsp::Limiter does.
// Threshold bakes in automatic makeup gain: the output volume is 10^(10*(1 - 1/ratio)/40) *
// dB2gain(-threshold) with a fixed ratio of 4, so a LOWER threshold (more limiting) also raises
// the makeup - a loudness-maximizing limiter, not a passive ceiling. ModuleGainAudit
// (Tests/Engine/GainStaging/ModuleGainAuditTests.cpp) measures a net worst case of +7.61 dB
// output/input RMS at threshold's minimum (-20 dB); allow-listed there, not a LimiterModule bug.
// The hard clip makes 0 dBFS the effective ceiling today; Ceiling adds a brickwall stage below it
// and is out of the path at its default of 0 dBFS. See docs/modules/fx-modules.md#limiter-module.
class LimiterModule
    : public ModuleBase
    , public GainReductionMeterSource {
public:
    static constexpr int kCeilingCvChannel = 5; // appended after Input Gain

    LimiterModule()
        : ModuleBase("Limiter", 6, 2) { // 2 audio + 4 CV (Threshold, Release, Input Gain, Ceiling)
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -20.0f, 0.0f, -1.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 1.0f, 500.0f, 100.0f));
        addParameter(inputGainParam =
                         new juce::AudioParameterFloat("inputGain", "Input Gain (dB)", -20.0f, 20.0f, 0.0f));
        addParameter(ceilingParam = new juce::AudioParameterFloat("ceiling", "Ceiling (dB)", -24.0f, 0.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        sampleRate_ = sampleRate;
        firstStage.prepare(spec);
        secondStage.prepare(spec);
        updateStages();
        resetStages();

        smoothedInputGain.reset(sampleRate, 0.005);
        smoothedInputGain.setCurrentAndTargetValue(*inputGainParam);
        // Threshold is where the gain computer starts pulling the signal down, so stepping it
        // steps the applied gain reduction. Advanced a block at a time (juce::dsp::Limiter only
        // takes it through setThreshold), snapped at prepare so a static render is unchanged.
        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        ceilingGain = 1.0f;
        meter.clear();
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Borrow Left into Right's raw channel, sample-exact, while Dual I/O is
        // split and only Left is patched. Must run before the bypass/mute branches below so
        // both see a filled Right leg exactly as if the user had cabled it.
        applyLeftRightNormalling(buffer);

        if (isBypassed()) {
            // Pass dry audio through; clear CV channels so mod signals don't leak downstream
            for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                buffer.clear(ch, 0, buffer.getNumSamples());
            meter.clear();
            return;
        }
        if (isMuted()) {
            buffer.clear();
            meter.clear();
            return;
        }

        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0 || buffer.getNumChannels() < 2)
            return;

        smoothedInputGain.setTargetValue(*inputGainParam);
        smoothedThreshold.setTargetValue(*thresholdParam);
        // CV (ch2-4) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units),
        // read once per block: Threshold and Release land in juce::dsp::Limiter setters that are
        // only consulted per block, and Input Gain is a per-block dB shift on the smoothed ramp.
        const float thresholdDb =
            modulateNormalised(*thresholdParam, smoothedThreshold.getCurrentValue(), blockCV(buffer, 2));
        const float releaseMs = modulateNormalised(*releaseParam, *releaseParam, blockCV(buffer, 3));
        setStageParameters(thresholdDb, releaseMs);
        // Release (read above) is a detector time constant: stepping it changes how fast gain
        // recovers, never the current gain. Deliberately not smoothed.
        smoothedThreshold.skip(numSamples);
        // Ceiling is not smoothed: the stage reacts to a lower ceiling the way it reacts to a louder
        // peak (down at once), and a higher one is given back over the release time.
        const float ceilingDb = modulateNormalised(*ceilingParam, *ceilingParam, blockCV(buffer, kCeilingCvChannel));

        // Apply input gain per-sample (smoothed)
        const float inputGainCV = blockCV(buffer, 4);
        float inputPeak = 0.0f;
        for (int i = 0; i < numSamples; ++i) {
            const float gainDb = modulateNormalised(*inputGainParam, smoothedInputGain.getNextValue(), inputGainCV);
            const float gain = juce::Decibels::decibelsToGain(gainDb);
            buffer.getWritePointer(0)[i] *= gain;
            buffer.getWritePointer(1)[i] *= gain;
            inputPeak =
                std::max(inputPeak, std::max(std::abs(buffer.getSample(0, i)), std::abs(buffer.getSample(1, i))));
        }

        // Only the audio pair goes through the limiter: it was prepared for 2 channels, and the
        // CV block behind it is not audio.
        juce::dsp::AudioBlock<float> fullBlock(buffer);
        juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
        const float stageReductionDb = processStages(audioBlock, inputPeak);
        const float ceilingReductionDb = applyCeiling(buffer, ceilingDb, releaseMs);
        meter.push(stageReductionDb + ceilingReductionDb, numSamples, sampleRate_);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Threshold", "Release", "Input Gain", "Ceiling"};
        return stereoInputLabel(i, kNumCvInputs, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(kNumCvInputs); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, kNumCvInputs); }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Threshold"
    // is the jack label, "Threshold (dB)" the knob) so the card rings it and accepts a drop on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", 2, "threshold"},
                {"Release", 3, "release"},
                {"Input Gain", 4, "inputGain"},
                {"Ceiling", kCeilingCvChannel, "ceiling"}};
    }

    float getGainReductionDb() const override { return meter.get(); }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Limiter; }

private:
    static constexpr int kNumCvInputs = 4;

    // The two stages of juce::dsp::Limiter, in its order: a gentle 4:1 stage at -10 dB, then the
    // 1000:1 stage at the Threshold, then the smoothed output volume and a clip at +-1. Returns the
    // reduction the two stages took, in dB, as the input peak against the peak they left (before
    // the output volume, which is makeup and not reduction).
    float processStages(juce::dsp::AudioBlock<float>& block, float inputPeak) {
        juce::dsp::ProcessContextReplacing<float> context(block);
        firstStage.process(context);
        secondStage.process(context);

        float stagePeak = 0.0f;
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch) {
            const auto range =
                juce::FloatVectorOperations::findMinAndMax(block.getChannelPointer(ch), (int)block.getNumSamples());
            stagePeak = std::max(stagePeak, std::max(std::abs(range.getStart()), std::abs(range.getEnd())));
        }

        block.multiplyBy(outputVolume);
        for (size_t ch = 0; ch < block.getNumChannels(); ++ch)
            juce::FloatVectorOperations::clip(block.getChannelPointer(ch), block.getChannelPointer(ch), -1.0f, 1.0f,
                                              (int)block.getNumSamples());

        if (inputPeak <= 1.0e-6f || stagePeak <= 1.0e-9f)
            return 0.0f;
        return juce::Decibels::gainToDecibels(inputPeak / stagePeak, 0.0f); // floored at 0 dB
    }

    void setStageParameters(float thresholdDb, float releaseMs) {
        stageThresholdDb = thresholdDb;
        stageReleaseMs = releaseMs;
        updateStages();
    }

    void updateStages() {
        firstStage.setThreshold(-10.0f);
        firstStage.setRatio(4.0f);
        firstStage.setAttack(2.0f);
        firstStage.setRelease(200.0f);
        secondStage.setThreshold(stageThresholdDb);
        secondStage.setRatio(1000.0f);
        secondStage.setAttack(0.001f);
        secondStage.setRelease(stageReleaseMs);
        const float makeup = (float)std::pow(10.0, 10.0 * (1.0 - 0.25) / 40.0) *
                             juce::Decibels::decibelsToGain(-stageThresholdDb, -100.0f);
        outputVolume.setTargetValue(makeup);
    }

    void resetStages() {
        firstStage.reset();
        secondStage.reset();
        outputVolume.reset(sampleRate_, 0.001);
    }

    // Brickwall stage: one linked gain, down at once to whatever keeps the louder leg at or under
    // the ceiling, back up over the release time. Out of the path (and reset) while the ceiling
    // sits at 0 dBFS and the gain is back at unity. Returns the deepest reduction it took, in dB.
    float applyCeiling(juce::AudioBuffer<float>& buffer, float ceilingDb, float releaseMs) {
        const bool active = ceilingDb < -1.0e-4f;
        if (!active && ceilingGain >= 0.9999f) {
            ceilingGain = 1.0f;
            return 0.0f;
        }
        // A ceiling returning to 0 dBFS lets the gain drift back to unity rather than snapping.
        const float ceiling = active ? juce::Decibels::decibelsToGain(ceilingDb) * 0.99999f : 1.0e9f;
        const float recovery = 1.0f - std::exp(-1.0f / std::max(1.0f, releaseMs * 0.001f * (float)sampleRate_));
        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        float deepest = 1.0f;
        for (int i = 0, n = buffer.getNumSamples(); i < n; ++i) {
            const float peak = std::max(std::abs(left[i]), std::abs(right[i]));
            const float needed = peak > ceiling ? ceiling / peak : 1.0f;
            ceilingGain = std::min(needed, ceilingGain + (1.0f - ceilingGain) * recovery);
            left[i] *= ceilingGain;
            right[i] *= ceilingGain;
            deepest = std::min(deepest, ceilingGain);
        }
        return -juce::Decibels::gainToDecibels(deepest, -100.0f);
    }

    juce::dsp::Compressor<float> firstStage, secondStage;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> outputVolume;
    float stageThresholdDb = -10.0f, stageReleaseMs = 100.0f;
    synth::GainReductionMeter meter;
    double sampleRate_ = 44100.0;
    float ceilingGain = 1.0f;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedInputGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* inputGainParam;
    juce::AudioParameterFloat* ceilingParam;
};
