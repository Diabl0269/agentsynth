#pragma once

#include "../ModuleBase.h"
#include "Modules/ParameterText.h"
#include <juce_audio_basics/juce_audio_basics.h>

// One Mix control (0 = dry, 1 = wet tail), like every other blending effect. The juce::Reverb
// inside is run wet-only at unity tail gain: JUCE's own wetScaleFactor = 3.0 is cancelled by feeding it
// wetLevel = 1/3, so Mix is a plain crossfade and Level scales the sum. Projects saved with the old Wet/Dry
// knobs load through adoptLegacyParams (mix = b/(a+b), level = min(1, a+b), a = 2*dry, b = 3*wet, JUCE's
// own factors); the default pair (0.33 / 0.4) lands up to ~5 dB quieter, accepted before v1.
// See docs/modules/fx-modules.md#reverb-module.
class ReverbModule : public ModuleBase {
public:
    static constexpr float kDefaultMix = 0.3f;
    static constexpr int kMixCvChannel = 4;
    static constexpr int kRetiredDryCvChannel = 5;
    static constexpr int kLevelCvChannel = 8;
    static constexpr int kPanCvChannel = 9;

    ReverbModule()
        // 2 audio + 8 raw CV channels: Size, Damping, Mix, (retired Dry), Width, Pre-Delay, Level, Pan. The retired
        // Dry channel stays so no saved cable moves; it is reserved, never drawn as a jack.
        : ModuleBase("Reverb", 10, 2) {
        addParameter(roomSizeParam = new juce::AudioParameterFloat("roomSize", "Room Size",
                                                                   juce::NormalisableRange<float>(0.0f, 1.0f), 0.5f,
                                                                   synth::fractionPercentAttributes()));
        addParameter(dampingParam =
                         new juce::AudioParameterFloat("damping", "Damping", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                       0.5f, synth::fractionPercentAttributes()));
        addParameter(widthParam =
                         new juce::AudioParameterFloat("width", "Width", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                       1.0f, synth::fractionPercentAttributes()));
        // Mix: 0 = dry only, 1 = reverb tail only (id and look shared with the other blending effects).
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                              kDefaultMix, synth::fractionPercentAttributes()));
        // Pre-Delay: a delay in front of the reverb's wet path only (the dry signal is not delayed).
        // 0 ms is the plain reverb, sample for sample.
        addParameter(preDelayParam = new juce::AudioParameterFloat("preDelay", "Pre-Delay (ms)", 0.0f, 250.0f, 0.0f));
        addOutputLevelParameter(1.0f, kLevelCvChannel);
        addMuteParameter();
        addPanParameter(kPanCvChannel); // shared output stage: this module keeps its own Mix
        allocateBuffers(44100.0, 512);  // usable before prepareToPlay, like the juce::Reverb it wraps
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        allocateBuffers(sampleRate, samplesPerBlock);
        prepareOutputLevel(sampleRate);
    }

    void processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Borrow Left into Right's raw channel, sample-exact, while Dual I/O is
        // split and only Left is patched. Must run before the bypass/mute branches below so
        // both see a filled Right leg exactly as if the user had cabled it.
        applyLeftRightNormalling(buffer);

        if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
            return;
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

        // CV (ch2-7) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units),
        // read once per block — juce::Reverb only takes its parameters per block anyway (and
        // ramps wet/dry/width internally). These jacks were declared as targets long before the
        // module had the channels; the ModulationTargetBinding sweep is what caught it.
        juce::Reverb::Parameters params;
        params.roomSize = modulateNormalised(*roomSizeParam, *roomSizeParam, blockCV(buffer, 2));
        params.damping = modulateNormalised(*dampingParam, *dampingParam, blockCV(buffer, 3));
        params.width = modulateNormalised(*widthParam, *widthParam, blockCV(buffer, 6));
        // Wet-only at unity tail gain (see the header comment); the dry pass and the crossfade are done here so
        // the dry signal skips the pre-delay.
        params.wetLevel = kJuceWetUnity;
        params.dryLevel = 0.0f;
        reverb.setParameters(params);
        smoothedMix.setTargetValue(modulateNormalised(*mixParam, *mixParam, blockCV(buffer, kMixCvChannel)));
        smoothedPreDelay.setTargetValue(modulateNormalised(*preDelayParam, *preDelayParam, blockCV(buffer, 7)));

        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
        for (int start = 0; start < buffer.getNumSamples(); start += dryScratch.getNumSamples())
            renderChunk(buffer, numChannels, start,
                        juce::jmin(dryScratch.getNumSamples(), buffer.getNumSamples() - start));

        // Scales the crossfaded sum: Mix sets the balance, Level how loud it lands downstream.
        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Size", "Damping", "Mix", "Width", "Pre-Delay", "Level", "Pan"};
        return stereoInputLabel(i, kVisibleCv, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(kVisibleCv); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override {
        // The retired Dry channel has no jack: channels past it sit one jack earlier.
        if (raw == kRetiredDryCvChannel) {
            LogicalPort p = mapStereoPairInput(kMixCvChannel, kVisibleCv);
            p.role = PortRole::Other;
            p.isPolyGroupHead = false;
            return p;
        }
        return mapStereoPairInput(raw > kRetiredDryCvChannel ? raw - 1 : raw, kVisibleCv);
    }
    bool isReservedInputChannel(int raw) const override { return raw == kRetiredDryCvChannel; }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // paramId binds "Size" to the "Room Size" knob. Level and Pan are appended last and reached through their knobs.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Size", 2, "roomSize"},
                {"Damping", 3, "damping"},
                {"Mix", kMixCvChannel, "mix"},
                {"Width", 6, "width"},
                {"Pre-Delay", 7, "preDelay"},
                {"Level", kLevelCvChannel, "outputLevel", true},
                {"Pan", kPanCvChannel, "pan", true}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Reverb; }

    /** A node saved with the old Wet/Dry knobs (or a patch that names them) becomes Mix + Level, so the balance and
     *  loudness carry over: old output = L * (a*in + b*tail) with a = 2*dry and b = 3*wet; new output =
     *  Level * ((1 - mix)*in + mix*tail). mix = b/(a+b); Level = L * min(1, a+b), so a sum above 1 (the default
     *  pair gives 1.79) lands quieter by 1/(a+b), up to ~5 dB. A node that already has `mix` is left alone. */
    void adoptLegacyParams(const juce::NamedValueSet& params) override {
        if (params.contains("mix") || !(params.contains("wet") || params.contains("dry")))
            return;
        const auto read = [&params](const char* key, float fallback) {
            const juce::var v = params[key];
            return juce::jlimit(0.0f, 1.0f, v.isVoid() ? fallback : (float)v);
        };
        float mix = 0.0f, levelScale = 1.0f;
        legacyMixAndLevel(read("dry", kLegacyDryDefault), read("wet", kLegacyWetDefault), mix, levelScale);
        mixParam->setValueNotifyingHost(mixParam->convertTo0to1(mix));
        if (outputLevelParam != nullptr)
            outputLevelParam->setValueNotifyingHost(
                outputLevelParam->convertTo0to1(juce::jlimit(0.0f, 1.0f, outputLevelParam->get() * levelScale)));
    }
    bool isLegacyParamId(const juce::String& id) const override { return id == "wet" || id == "dry"; }

    /** The Mix and the Level factor an old Wet/Dry pair becomes (see adoptLegacyParams). */
    static void legacyMixAndLevel(float dry, float wet, float& mix, float& levelScale) {
        const float a = kJuceDryScale * dry;
        const float b = kJuceWetScale * wet;
        const float sum = a + b;
        mix = sum > 0.0f ? b / sum : kDefaultMix;
        levelScale = juce::jmin(1.0f, sum);
    }

    /** Module state from a plugin host saved before Mix existed holds `wet` / `dry` attributes. */
    void setStateInformation(const void* data, int sizeInBytes) override {
        ModuleBase::setStateInformation(data, sizeInBytes);
        if (auto xml = getXmlFromBinary(data, sizeInBytes); xml != nullptr && xml->hasTagName("ModuleState")) {
            juce::NamedValueSet legacy;
            for (const char* key : {"mix", "wet", "dry"})
                if (xml->hasAttribute(key))
                    legacy.set(key, xml->getDoubleAttribute(key));
            adoptLegacyParams(legacy);
        }
    }

private:
    // juce::Reverb's private scale factors (JUCE's code): the old Wet knob reached the tail at 3x, Dry the input at 2x.
    static constexpr float kJuceDryScale = 2.0f;
    static constexpr float kJuceWetScale = 3.0f;
    static constexpr float kJuceWetUnity = 1.0f / kJuceWetScale; // wetLevel that gives the tail unity gain
    static constexpr float kLegacyWetDefault = 0.33f;
    static constexpr float kLegacyDryDefault = 0.4f;
    static constexpr int kVisibleCv = 7; // Size, Damping, Mix, Width, Pre-Delay, Level, Pan
    static constexpr double kMixSmoothSeconds = 0.01;
    static constexpr double kMaxPreDelaySeconds = 0.25;

    /** Allocates every buffer the audio thread uses and resets the smoothers. Message thread only. */
    void allocateBuffers(double sampleRate, int samplesPerBlock) {
        sr = sampleRate;
        // juce::Reverb resets its smoothers in setSampleRate, so set its wet-only, unity-tail state first: it then
        // starts there instead of ramping from its defaults (wet 0.33 * 3, dry 0.4 * 2).
        juce::Reverb::Parameters wetOnly;
        wetOnly.wetLevel = kJuceWetUnity;
        wetOnly.dryLevel = 0.0f;
        reverb.setParameters(wetOnly);
        reverb.setSampleRate(sampleRate);

        preDelayLine.setSize(2, (int)(kMaxPreDelaySeconds * sampleRate) + 4);
        preDelayLine.clear();
        preDelayWritePos = 0;
        dryScratch.setSize(2, juce::jmax(1, samplesPerBlock));

        smoothedMix.reset(sampleRate, kMixSmoothSeconds);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        smoothedPreDelay.reset(sampleRate, 0.05); // 50 ms ramp, like Delay's Time: a jump would zip
        smoothedPreDelay.setCurrentAndTargetValue(*preDelayParam);
    }

    /** One chunk of the block: pre-delay the wet feed, run the reverb, crossfade with the undelayed dry. */
    void renderChunk(juce::AudioBuffer<float>& buffer, int numChannels, int start, int count) {
        const int ringSize = preDelayLine.getNumSamples();
        int writePos = preDelayWritePos;
        for (int i = 0; i < count; ++i) {
            const float delaySamples = smoothedPreDelay.getNextValue() * 0.001f * (float)sr;
            for (int ch = 0; ch < numChannels; ++ch) {
                float* data = buffer.getWritePointer(ch) + start;
                float* ring = preDelayLine.getWritePointer(ch);
                const float in = data[i];
                ring[writePos] = in;
                dryScratch.getWritePointer(ch)[i] = in;
                if (delaySamples > 0.0f)
                    data[i] = readPreDelayed(ring, ringSize, writePos, delaySamples);
            }
            writePos = (writePos + 1) % ringSize;
        }
        preDelayWritePos = writePos;

        if (numChannels >= 2)
            reverb.processStereo(buffer.getWritePointer(0) + start, buffer.getWritePointer(1) + start, count);
        else
            reverb.processMono(buffer.getWritePointer(0) + start, count);

        for (int i = 0; i < count; ++i) {
            const float mix = smoothedMix.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch) {
                float& out = buffer.getWritePointer(ch)[start + i];
                out = out * mix + dryScratch.getReadPointer(ch)[i] * (1.0f - mix);
            }
        }
    }

public:
    /** The pre-delay tap, `delaySamples` behind `writePos`, interpolated. Wraps on integer indices: a float
     *  position a hair below 0 plus ringSize rounds to ringSize, a read past the end that fed garbage into the tail. */
    static float readPreDelayed(const float* ring, int ringSize, int writePos, float delaySamples) {
        const float delay = juce::jlimit(0.0f, (float)(ringSize - 2), delaySamples);
        const int whole = (int)delay;
        const float frac = delay - (float)whole;
        int newer = writePos - whole;
        if (newer < 0)
            newer += ringSize;
        const int older = newer > 0 ? newer - 1 : ringSize - 1;
        return ring[newer] * (1.0f - frac) + ring[older] * frac;
    }

private:
    double sr = 44100.0;
    juce::AudioBuffer<float> preDelayLine; // 250 ms of history per leg, written every sample
    juce::AudioBuffer<float> dryScratch;   // the undelayed input of the current chunk
    int preDelayWritePos = 0;
    juce::SmoothedValue<float> smoothedMix;
    juce::SmoothedValue<float> smoothedPreDelay; // ms
    juce::Reverb reverb;
    juce::AudioParameterFloat* roomSizeParam;
    juce::AudioParameterFloat* dampingParam;
    juce::AudioParameterFloat* mixParam;
    juce::AudioParameterFloat* widthParam;
    juce::AudioParameterFloat* preDelayParam;
};
