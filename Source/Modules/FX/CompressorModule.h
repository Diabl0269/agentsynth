#pragma once

#include "../ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

// Stereo compressor with a Key (sidechain) input: while a cable reaches Key L/R the detector listens to
// the key instead of the module's own audio. See docs/modules/fx-modules.md#compressor-module.
class CompressorModule : public ModuleBase {
public:
    static constexpr int kNumCvInputs = 5;
    static constexpr int kKeyBase = sidechainKeyBase(kNumCvInputs); // Key L = 7, Key R = 8

    CompressorModule()
        : ModuleBase("Compressor", 9, 2) { // 2 audio + 5 CV (Threshold, Ratio, Attack, Release, Makeup) + Key L/R
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -60.0f, 0.0f, -12.0f));
        addParameter(ratioParam = new juce::AudioParameterFloat("ratio", "Ratio", 1.0f, 20.0f, 4.0f));
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack (ms)", 0.1f, 200.0f, 10.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 10.0f, 1000.0f, 100.0f));
        addParameter(makeupGainParam =
                         new juce::AudioParameterFloat("makeupGain", "Makeup Gain (dB)", -20.0f, 40.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        compressor.prepare(spec);
        compressor.reset();

        // The keyed detector: one linked envelope (see processKeyed), peak-rectified like the
        // juce::dsp::Compressor's own BallisticsFilter so a key behaves like the audio it replaces.
        spec.numChannels = 1;
        keyEnvelope.prepare(spec);
        keyEnvelope.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
        keyEnvelope.reset();
        wasKeyed = false;

        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedMakeupGain.reset(sampleRate, 0.005);
        // Ratio sets the slope of the gain computer, so stepping it steps the applied gain
        // reduction exactly like Threshold does. Same 10 ms block-rate treatment.
        smoothedRatio.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        smoothedMakeupGain.setCurrentAndTargetValue(*makeupGainParam);
        smoothedRatio.setCurrentAndTargetValue(*ratioParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Borrow Left into Right's raw channel, sample-exact, while Dual I/O is
        // split and only Left is patched. Must run before the bypass/mute branches below so
        // both see a filled Right leg exactly as if the user had cabled it.
        applyLeftRightNormalling(buffer);

        if (isBypassed()) {
            // Pass dry audio through; clear CV channels so mod signals don't leak downstream
            for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
                buffer.clear(ch, 0, buffer.getNumSamples());
            return;
        }
        if (isMuted()) {
            buffer.clear();
            return;
        }

        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0 || buffer.getNumChannels() < 2)
            return;

        smoothedThreshold.setTargetValue(*thresholdParam);
        smoothedMakeupGain.setTargetValue(*makeupGainParam);
        smoothedRatio.setTargetValue(*ratioParam);

        // CV (ch2-6) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units)
        // and is read once per block — every target lands in a juce::dsp::Compressor setter that
        // is itself only consulted per block. Threshold/Ratio CV rides on top of the smoothed
        // base, so a knob move still ramps while the CV steps.
        const float thresholdDb =
            modulateNormalised(*thresholdParam, smoothedThreshold.getCurrentValue(), blockCV(buffer, 2));
        const float ratio = modulateNormalised(*ratioParam, smoothedRatio.getCurrentValue(), blockCV(buffer, 3));
        // Attack/Release are detector time constants, not levels: stepping one changes how fast
        // the envelope follower tracks, never the current gain. Deliberately not smoothed.
        const float attackMs = modulateNormalised(*attackParam, *attackParam, blockCV(buffer, 4));
        const float releaseMs = modulateNormalised(*releaseParam, *releaseParam, blockCV(buffer, 5));
        compressor.setThreshold(thresholdDb);
        compressor.setRatio(ratio);
        compressor.setAttack(attackMs);
        compressor.setRelease(releaseMs);

        // Keyed only while a cable reaches a Key jack (published from the message thread — never
        // inferred from the key's level) and the buffer actually carries the key channels.
        const bool keyed = isSidechainConnected() && buffer.getNumChannels() >= kKeyBase + 2;
        if (keyed != wasKeyed) {
            // Each detector resumes from rest rather than from a stale envelope of another signal.
            compressor.reset();
            keyEnvelope.reset();
            wasKeyed = keyed;
        }

        if (keyed) {
            processKeyed(buffer, thresholdDb, ratio, attackMs, releaseMs);
        } else {
            // Only the audio pair goes through the compressor: it was prepared for 2 channels, and
            // the CV block behind it is not audio.
            juce::dsp::AudioBlock<float> fullBlock(buffer);
            juce::dsp::AudioBlock<float> audioBlock = fullBlock.getSubsetChannelBlock(0, 2);
            juce::dsp::ProcessContextReplacing<float> context(audioBlock);
            compressor.process(context);
        }

        // Apply makeup gain per-sample (smoothed); the CV offset is a per-block dB shift on top.
        const float makeupCV = blockCV(buffer, 6);
        for (int i = 0; i < numSamples; ++i) {
            const float makeupDb = modulateNormalised(*makeupGainParam, smoothedMakeupGain.getNextValue(), makeupCV);
            const float linearGain = juce::Decibels::decibelsToGain(makeupDb);
            buffer.getWritePointer(0)[i] *= linearGain;
            buffer.getWritePointer(1)[i] *= linearGain;
        }

        smoothedThreshold.skip(numSamples);
        smoothedRatio.skip(numSamples);

        // Clear CV and key channels (read above) to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Threshold", "Ratio", "Attack", "Release", "Makeup"};
        return stereoKeyInputLabel(i, kNumCvInputs, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoKeyVisibleInputCount(kNumCvInputs); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoKeyInput(raw, kNumCvInputs); }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Threshold"
    // is the jack label, "Threshold (dB)" the knob) so the card rings it and accepts a drop on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", 2, "threshold"},
                {"Ratio", 3, "ratio"},
                {"Attack", 4, "attack"},
                {"Release", 5, "release"},
                {"Makeup", 6, "makeupGain"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Compressor; }

private:
    // The keyed path: the same peak ballistics and gain law as juce::dsp::Compressor::processSample,
    // but ONE envelope driven by max(|Key L|, |Key R|) and applied to both legs, so a mono key
    // patched into Key L alone still ducks the whole stereo image.
    void processKeyed(juce::AudioBuffer<float>& buffer, float thresholdDb, float ratio, float attackMs,
                      float releaseMs) {
        keyEnvelope.setAttackTime(attackMs);
        keyEnvelope.setReleaseTime(releaseMs);
        const float threshold = juce::Decibels::decibelsToGain(thresholdDb, -200.0f);
        const float thresholdInverse = 1.0f / threshold;
        const float ratioInverse = 1.0f / ratio;

        const float* keyL = buffer.getReadPointer(kKeyBase);
        const float* keyR = buffer.getReadPointer(kKeyBase + 1);
        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0, n = buffer.getNumSamples(); i < n; ++i) {
            const float env = keyEnvelope.processSample(0, std::max(std::abs(keyL[i]), std::abs(keyR[i])));
            const float gain = env < threshold ? 1.0f : std::pow(env * thresholdInverse, ratioInverse - 1.0f);
            left[i] *= gain;
            right[i] *= gain;
        }
        keyEnvelope.snapToZero();
    }

    juce::dsp::Compressor<float> compressor;
    juce::dsp::BallisticsFilter<float> keyEnvelope;
    bool wasKeyed = false;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMakeupGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRatio;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* ratioParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* makeupGainParam;
};
