#pragma once

#include "../GainReductionMeterSource.h"
#include "../ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

// Stereo compressor with a Key (sidechain) input: while a cable reaches Key L/R the detector listens to
// the key instead of the module's own audio. See docs/modules/fx-modules.md#compressor-module.
class CompressorModule
    : public ModuleBase
    , public GainReductionMeterSource {
public:
    static constexpr int kNumCvInputs = 5;
    static constexpr int kKeyBase = sidechainKeyBase(kNumCvInputs); // Key L = 7, Key R = 8
    static constexpr int kKneeCvChannel = kKeyBase + 2;             // appended after the key pair

    CompressorModule()
        : ModuleBase("Compressor", 10,
                     2) { // 2 audio + 5 CV (Threshold, Ratio, Attack, Release, Makeup) + Key L/R + Knee
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -60.0f, 0.0f, -12.0f));
        addParameter(ratioParam = new juce::AudioParameterFloat("ratio", "Ratio", 1.0f, 20.0f, 4.0f));
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack (ms)", 0.1f, 200.0f, 10.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 10.0f, 1000.0f, 100.0f));
        addParameter(makeupGainParam =
                         new juce::AudioParameterFloat("makeupGain", "Makeup Gain (dB)", -20.0f, 40.0f, 0.0f));
        // Width of the soft knee centred on the threshold; 0 is the hard knee every patch had before.
        addParameter(kneeParam = new juce::AudioParameterFloat("knee", "Knee (dB)", 0.0f, 24.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = (juce::uint32)samplesPerBlock;
        spec.numChannels = 2;
        // Own detector: one peak envelope per leg, the ballistics juce::dsp::Compressor used.
        envelope.prepare(spec);
        envelope.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
        envelope.reset();

        // The keyed detector: one linked envelope (see processKeyed), peak-rectified like the own
        // detector so a key behaves like the audio it replaces.
        spec.numChannels = 1;
        keyEnvelope.prepare(spec);
        keyEnvelope.setLevelCalculationType(juce::dsp::BallisticsFilterLevelCalculationType::peak);
        keyEnvelope.reset();
        wasKeyed = false;
        meter.clear();
        sampleRate_ = sampleRate;

        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedMakeupGain.reset(sampleRate, 0.005);
        // Ratio sets the slope of the gain computer, so stepping it steps the applied gain
        // reduction exactly like Threshold does. Same 10 ms block-rate treatment.
        smoothedRatio.reset(sampleRate, 0.01);
        // The knee width shapes the same gain law, so a step in it steps the applied reduction too.
        smoothedKnee.reset(sampleRate, 0.01);
        smoothedKnee.setCurrentAndTargetValue(*kneeParam);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        smoothedMakeupGain.setCurrentAndTargetValue(*makeupGainParam);
        smoothedRatio.setCurrentAndTargetValue(*ratioParam);
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

        smoothedThreshold.setTargetValue(*thresholdParam);
        smoothedMakeupGain.setTargetValue(*makeupGainParam);
        smoothedRatio.setTargetValue(*ratioParam);
        smoothedKnee.setTargetValue(*kneeParam);

        // CV (ch2-6, Knee ch9) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units)
        // and is read once per block. Threshold/Ratio/Knee CV rides on top of the smoothed
        // base, so a knob move still ramps while the CV steps.
        const float thresholdDb =
            modulateNormalised(*thresholdParam, smoothedThreshold.getCurrentValue(), blockCV(buffer, 2));
        const float ratio = modulateNormalised(*ratioParam, smoothedRatio.getCurrentValue(), blockCV(buffer, 3));
        const float kneeDb =
            modulateNormalised(*kneeParam, smoothedKnee.getCurrentValue(), blockCV(buffer, kKneeCvChannel));
        // Attack/Release are detector time constants, not levels: stepping one changes how fast
        // the envelope follower tracks, never the current gain. Deliberately not smoothed.
        const float attackMs = modulateNormalised(*attackParam, *attackParam, blockCV(buffer, 4));
        const float releaseMs = modulateNormalised(*releaseParam, *releaseParam, blockCV(buffer, 5));
        envelope.setAttackTime(attackMs);
        envelope.setReleaseTime(releaseMs);
        keyEnvelope.setAttackTime(attackMs);
        keyEnvelope.setReleaseTime(releaseMs);
        gainLaw.set(thresholdDb, ratio, kneeDb);

        // Keyed only while a cable reaches a Key jack (published from the message thread — never
        // inferred from the key's level) and the buffer actually carries the key channels.
        const bool keyed = isSidechainConnected() && buffer.getNumChannels() >= kKeyBase + 2;
        if (keyed != wasKeyed) {
            // Each detector resumes from rest rather than from a stale envelope of another signal.
            envelope.reset();
            keyEnvelope.reset();
            wasKeyed = keyed;
        }

        const float deepestGain = keyed ? processKeyed(buffer) : processOwn(buffer);
        meter.push(-juce::Decibels::gainToDecibels(deepestGain, -100.0f), numSamples, sampleRate_);

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
        smoothedKnee.skip(numSamples);

        // Clear CV and key channels (read above) to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Threshold", "Ratio", "Attack", "Release", "Makeup"};
        if (i == kneeJack())
            return "Knee";
        return stereoKeyInputLabel(i, kNumCvInputs, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return kneeJack() + 1; }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override {
        if (raw != kKneeCvChannel)
            return mapStereoKeyInput(raw, kNumCvInputs);
        LogicalPort p;
        p.visibleJackIndex = kneeJack();
        p.role = PortRole::ModCV;
        p.isPolyGroupHead = true;
        p.polyVoiceSpan = 1;
        return p;
    }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds each jack to its knob ("Threshold"
    // is the jack label, "Threshold (dB)" the knob) so the card rings it and accepts a drop on it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Threshold", 2, "threshold"}, {"Ratio", 3, "ratio"},       {"Attack", 4, "attack"},
                {"Release", 5, "release"},     {"Makeup", 6, "makeupGain"}, {"Knee", kKneeCvChannel, "knee"}};
    }

    float getGainReductionDb() const override { return meter.get(); }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Compressor; }

private:
    /** Visible jack of the Knee CV: the jack after the audio pair, the CV jacks and the key. */
    int kneeJack() const { return stereoKeyVisibleInputCount(kNumCvInputs); }

    /** The static gain law. With no knee it is exactly juce::dsp::Compressor's (a hard knee: unity
        under the threshold, env/threshold to the power 1/ratio - 1 above), so a patch saved before
        Knee existed renders sample for sample as it did. A knee blends the two over `knee` dB
        centred on the threshold (the standard quadratic soft knee). */
    struct GainLaw {
        float thresholdDb = 0.0f, threshold = 1.0f, thresholdInverse = 1.0f, ratioInverse = 1.0f;
        float kneeDb = 0.0f, kneeStart = 1.0f;

        void set(float newThresholdDb, float ratio, float newKneeDb) {
            thresholdDb = newThresholdDb;
            threshold = juce::Decibels::decibelsToGain(newThresholdDb, -200.0f);
            thresholdInverse = 1.0f / threshold;
            ratioInverse = 1.0f / ratio;
            kneeDb = newKneeDb > 1.0e-3f ? newKneeDb : 0.0f;
            kneeStart = juce::Decibels::decibelsToGain(newThresholdDb - 0.5f * kneeDb, -200.0f);
        }

        float gain(float env) const noexcept {
            if (kneeDb == 0.0f)
                return env < threshold ? 1.0f : std::pow(env * thresholdInverse, ratioInverse - 1.0f);
            if (env < kneeStart)
                return 1.0f;
            const float over = juce::Decibels::gainToDecibels(env, -200.0f) - thresholdDb;
            const float slope = ratioInverse - 1.0f;
            const float below = over + 0.5f * kneeDb; // distance into the knee
            const float reductionDb = over >= 0.5f * kneeDb ? slope * over : slope * below * below / (2.0f * kneeDb);
            return std::exp(reductionDb * 0.115129255f); // ln(10) / 20
        }
    };

    // Own detector: each leg listens to itself, as juce::dsp::Compressor did. Returns the lowest
    // gain applied (1 = untouched) for the gain-reduction meter.
    float processOwn(juce::AudioBuffer<float>& buffer) {
        float deepest = 1.0f;
        for (int ch = 0; ch < 2; ++ch) {
            float* data = buffer.getWritePointer(ch);
            for (int i = 0, n = buffer.getNumSamples(); i < n; ++i) {
                const float gain = gainLaw.gain(envelope.processSample(ch, data[i]));
                data[i] = gain * data[i];
                deepest = std::min(deepest, gain);
            }
        }
        return deepest;
    }

    // The keyed path: the same peak ballistics and gain law, but ONE envelope driven by
    // max(|Key L|, |Key R|) and applied to both legs, so a mono key patched into Key L alone still
    // ducks the whole stereo image. Returns the lowest gain applied, as processOwn does.
    float processKeyed(juce::AudioBuffer<float>& buffer) {
        float deepest = 1.0f;
        const float* keyL = buffer.getReadPointer(kKeyBase);
        const float* keyR = buffer.getReadPointer(kKeyBase + 1);
        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        for (int i = 0, n = buffer.getNumSamples(); i < n; ++i) {
            const float env = keyEnvelope.processSample(0, std::max(std::abs(keyL[i]), std::abs(keyR[i])));
            const float gain = gainLaw.gain(env);
            left[i] *= gain;
            right[i] *= gain;
            deepest = std::min(deepest, gain);
        }
        keyEnvelope.snapToZero();
        return deepest;
    }

    GainLaw gainLaw;
    juce::dsp::BallisticsFilter<float> envelope;
    juce::dsp::BallisticsFilter<float> keyEnvelope;
    bool wasKeyed = false;
    double sampleRate_ = 44100.0;
    synth::GainReductionMeter meter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMakeupGain;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRatio;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedKnee;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* ratioParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* makeupGainParam;
    juce::AudioParameterFloat* kneeParam;
};
