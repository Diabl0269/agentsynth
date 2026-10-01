#pragma once

#include "../ModuleBase.h"
#include <juce_audio_basics/juce_audio_basics.h>

// Wet/Dry can exceed unity gain: juce::Reverb::updateParameters() (JUCE's code, not this
// module's) applies its own internal wetScaleFactor = 3.0 and dryScaleFactor = 2.0 to the 0-1
// Wet/Dry knobs before mixing, so neither is a plain 0-1 balance control at the signal level -
// maxing Wet feeds the algorithmic tail at up to 3x, maxing Dry passes the input at up to 2x.
// ModuleGainAudit (Tests/Engine/GainStaging/ModuleGainAuditTests.cpp) measures a worst case of
// +8.91 dB (Wet at max) and +7.77 dB (Dry at max) against a continuous, hot test tone;
// allow-listed there. See docs/modules/fx-modules.md#reverb-module.
class ReverbModule : public ModuleBase {
public:
    ReverbModule()
        : ModuleBase("Reverb", 8, 2) { // 2 audio + 6 CV (Size, Damping, Wet, Dry, Width, Pre-Delay)
        addParameter(roomSizeParam = new juce::AudioParameterFloat("roomSize", "Room Size", 0.0f, 1.0f, 0.5f));
        addParameter(dampingParam = new juce::AudioParameterFloat("damping", "Damping", 0.0f, 1.0f, 0.5f));
        addParameter(wetParam = new juce::AudioParameterFloat("wet", "Wet", 0.0f, 1.0f, 0.33f));
        addParameter(dryParam = new juce::AudioParameterFloat("dry", "Dry", 0.0f, 1.0f, 0.4f));
        addParameter(widthParam = new juce::AudioParameterFloat("width", "Width", 0.0f, 1.0f, 1.0f));
        // Pre-Delay: a delay in front of the reverb's wet path only (the dry signal is not delayed).
        // 0 ms is the plain reverb, sample for sample.
        addParameter(preDelayParam = new juce::AudioParameterFloat("preDelay", "Pre-Delay (ms)", 0.0f, 250.0f, 0.0f));
        addOutputLevelParameter();
        addMuteParameter();
        allocateBuffers(44100.0, 512); // usable before prepareToPlay, like the juce::Reverb it wraps
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
        params.wetLevel = modulateNormalised(*wetParam, *wetParam, blockCV(buffer, 4));
        params.dryLevel = modulateNormalised(*dryParam, *dryParam, blockCV(buffer, 5));
        params.width = modulateNormalised(*widthParam, *widthParam, blockCV(buffer, 6));
        // The dry pass is added here, not inside juce::Reverb: the reverb sees only the pre-delayed
        // signal, so its own dry gain is held at 0 and `dryGain` below replays juce::Reverb's dry law
        // (2x scale, 10 ms ramp) on the undelayed input. At Pre-Delay 0 that is the same arithmetic in
        // the same order, so the output is bit-identical to the plain reverb.
        dryGain.setTargetValue(params.dryLevel * kJuceDryScale);
        params.dryLevel = 0.0f;
        reverb.setParameters(params);
        smoothedPreDelay.setTargetValue(modulateNormalised(*preDelayParam, *preDelayParam, blockCV(buffer, 7)));

        const int numChannels = juce::jmin(buffer.getNumChannels(), 2);
        for (int start = 0; start < buffer.getNumSamples(); start += dryScratch.getNumSamples())
            renderChunk(buffer, numChannels, start,
                        juce::jmin(dryScratch.getNumSamples(), buffer.getNumSamples() - start));

        // Scales wet+dry together — Wet/Dry set the balance, Level sets how loud the
        // whole thing lands downstream.
        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Size", "Damping", "Wet", "Dry", "Width", "Pre-Delay"};
        return stereoInputLabel(i, 6, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(6); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 6); }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // Every continuous parameter has a CV jack; paramId binds "Size" to the "Room Size" knob.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Size", 2, "roomSize"}, {"Damping", 3, "damping"}, {"Wet", 4, "wet"},
                {"Dry", 5, "dry"},       {"Width", 6, "width"},     {"Pre-Delay", 7, "preDelay"}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Reverb; }

private:
    // juce::Reverb's private dryScaleFactor (see the header comment): the dry knob reaches the mix at 2x.
    static constexpr float kJuceDryScale = 2.0f;
    static constexpr double kJuceDrySmoothSeconds = 0.01;
    static constexpr double kMaxPreDelaySeconds = 0.25;

    /** Allocates every buffer the audio thread uses and resets the smoothers. Message thread only. */
    void allocateBuffers(double sampleRate, int samplesPerBlock) {
        sr = sampleRate;
        // juce::Reverb resets its smoothers in setSampleRate, so zero its dry gain first: it then
        // starts at 0 instead of ramping down from its default 0.4 * 2.
        juce::Reverb::Parameters wetOnly;
        wetOnly.dryLevel = 0.0f;
        reverb.setParameters(wetOnly);
        reverb.setSampleRate(sampleRate);

        preDelayLine.setSize(2, (int)(kMaxPreDelaySeconds * sampleRate) + 4);
        preDelayLine.clear();
        preDelayWritePos = 0;
        dryScratch.setSize(2, juce::jmax(1, samplesPerBlock));

        // Start where a fresh juce::Reverb's dry gain starts (default dry 0.4 at 2x), so the first
        // block ramps exactly as it always did.
        dryGain.reset(sampleRate, kJuceDrySmoothSeconds);
        dryGain.setCurrentAndTargetValue(juce::Reverb::Parameters().dryLevel * kJuceDryScale);
        smoothedPreDelay.reset(sampleRate, 0.05); // 50 ms ramp, like Delay's Time: a jump would zip
        smoothedPreDelay.setCurrentAndTargetValue(*preDelayParam);
    }

    /** One chunk of the block: pre-delay the wet feed, run the reverb, add the undelayed dry. */
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
                    data[i] = readRing(ring, ringSize, (float)writePos - delaySamples);
            }
            writePos = (writePos + 1) % ringSize;
        }
        preDelayWritePos = writePos;

        if (numChannels >= 2)
            reverb.processStereo(buffer.getWritePointer(0) + start, buffer.getWritePointer(1) + start, count);
        else
            reverb.processMono(buffer.getWritePointer(0) + start, count);

        for (int i = 0; i < count; ++i) {
            const float dry = dryGain.getNextValue();
            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer(ch)[start + i] += dryScratch.getReadPointer(ch)[i] * dry;
        }
    }

    static float readRing(const float* ring, int ringSize, float pos) {
        if (pos < 0.0f)
            pos += (float)ringSize;
        const int p0 = (int)pos;
        const float frac = pos - (float)p0;
        return ring[p0] * (1.0f - frac) + ring[(p0 + 1) % ringSize] * frac;
    }

    double sr = 44100.0;
    juce::AudioBuffer<float> preDelayLine; // 250 ms of history per leg, written every sample
    juce::AudioBuffer<float> dryScratch;   // the undelayed input of the current chunk
    int preDelayWritePos = 0;
    juce::SmoothedValue<float> dryGain;
    juce::SmoothedValue<float> smoothedPreDelay; // ms
    juce::Reverb reverb;
    juce::AudioParameterFloat* roomSizeParam;
    juce::AudioParameterFloat* dampingParam;
    juce::AudioParameterFloat* wetParam;
    juce::AudioParameterFloat* dryParam;
    juce::AudioParameterFloat* widthParam;
    juce::AudioParameterFloat* preDelayParam;
};
