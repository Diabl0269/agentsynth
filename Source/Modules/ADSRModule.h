#pragma once

#include "ModuleBase.h"
#include "SchmittTrigger.h"
#include "ThresholdMeterSource.h"
#include <algorithm>
#include <atomic>
#include <cmath>

class ADSRModule
    : public ModuleBase
    , public ThresholdMeterSource {
public:
    ADSRModule(const juce::String& name = "ADSR")
        : ModuleBase(name, 9, 9) // 8 gate CV per voice + shared Threshold CV; 8 env + silent ch8
    {
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack", 0.01f, 5.0f, 0.05f));
        addParameter(decayParam = new juce::AudioParameterFloat("decay", "Decay", 0.01f, 5.0f, 0.2f));
        addParameter(sustainParam = new juce::AudioParameterFloat("sustain", "Sustain", 0.0f, 1.0f, 0.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release", 0.01f, 5.0f, 0.1f));
        // `gateThreshold`, not `threshold` / `trigThreshold`: Compressor owns `threshold` as dB,
        // Sample & Hold / Comparator own `trigThreshold` as bipolar CV. ADSR gates are unipolar.
        addParameter(thresholdParam = new juce::AudioParameterFloat("gateThreshold", "Threshold", 0.0f, 1.0f, 0.5f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        for (int v = 0; v < MAX_VOICES; ++v) {
            adsrs[v].setSampleRate(sampleRate);
            adsrs[v].reset();
            gateTriggers[v].reset();
            previousActive[v] = false;
        }
        midiGateHeld = false;
        resetMeters();
        effectiveThreshold.store(thresholdParam->get(), std::memory_order_relaxed);
        // Sustain is the one ADSR parameter that is a LEVEL: juce::ADSR emits it verbatim while
        // the envelope is held (`case State::sustain: envelopeVal = parameters.sustain`), so a
        // per-block automation write steps every destination downstream. Attack/Decay/Release are
        // ramp *rates* — changing one alters the slope of an in-flight ramp, never its value —
        // and are deliberately left unsmoothed. The smoother advances a whole block at a time
        // because juce::ADSR only takes its parameters through setParameters(); snapped at prepare
        // so a static render is bit-identical.
        smoothedSustain.reset(sampleRate, 0.02);
        smoothedSustain.setCurrentAndTargetValue(sustainParam->get());
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
            return;

        // Pure source / CV generator: there is no dry audio path, so both branches clear.
        // Kept as two conditions so a fused `isBypassed() || isMuted()` cannot sneak back in.
        if (isBypassed()) {
            buffer.clear();
            resetMeters();
            return;
        }
        if (isMuted()) {
            buffer.clear();
            resetMeters();
            return;
        }

        smoothedSustain.setTargetValue(*sustainParam);

        float a = std::max(static_cast<float>(*attackParam), 0.002f);
        float d = *decayParam;
        float s = smoothedSustain.getCurrentValue();
        float r = std::max(static_cast<float>(*releaseParam), 0.005f);
        smoothedSustain.skip(buffer.getNumSamples());

        if (a != adsrParams.attack || d != adsrParams.decay || s != adsrParams.sustain || r != adsrParams.release) {
            adsrParams.attack = a;
            adsrParams.decay = d;
            adsrParams.sustain = s;
            adsrParams.release = r;
            for (int v = 0; v < MAX_VOICES; ++v)
                adsrs[v].setParameters(adsrParams);
        }

        const bool poly = *polyParam;
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();
        const float baseThreshold = thresholdParam->get();
        const float* thresholdCV = numChannels > kThresholdChannel ? buffer.getReadPointer(kThresholdChannel) : nullptr;

        float meterPeak = 0.0f;
        float lastThreshold = baseThreshold;
        int firedThisBlock = 0;

        if (!poly) {
            const float* gateIn = buffer.getReadPointer(0);
            float* envOut = buffer.getWritePointer(0);

            for (int smp = 0; smp < numSamples; ++smp) {
                for (const auto metadata : midiMessages) {
                    if (metadata.samplePosition != smp)
                        continue;
                    const auto message = metadata.getMessage();
                    if (message.isNoteOn())
                        midiGateHeld = true;
                    else if (message.isNoteOff())
                        midiGateHeld = false;
                }

                const float gateSample = gateIn[smp];
                if (std::abs(gateSample) > std::abs(meterPeak))
                    meterPeak = gateSample;

                float threshold = baseThreshold;
                if (thresholdCV != nullptr)
                    threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                lastThreshold = threshold;

                gateTriggers[0].process(gateSample, threshold);
                const bool active = midiGateHeld || gateTriggers[0].high;
                if (active && !previousActive[0]) {
                    adsrs[0].noteOn();
                    ++firedThisBlock;
                } else if (!active && previousActive[0]) {
                    adsrs[0].noteOff();
                }
                previousActive[0] = active;

                envOut[smp] = adsrs[0].getNextSample();
            }

            for (const auto metadata : midiMessages) {
                if (metadata.samplePosition < numSamples)
                    continue;
                const auto message = metadata.getMessage();
                if (message.isNoteOn())
                    midiGateHeld = true;
                else if (message.isNoteOff())
                    midiGateHeld = false;
            }

            for (int ch = 1; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);

            if (auto* vb = getVisualBuffer())
                for (int smp = 0; smp < numSamples; ++smp)
                    vb->pushSample(envOut[smp]);
        } else {
            for (int v = 0; v < MAX_VOICES; ++v)
                adsrs[v].setParameters(adsrParams);

            const int voices = std::min(MAX_VOICES, numChannels);
            for (int v = 0; v < voices; ++v) {
                float* data = buffer.getWritePointer(v);
                for (int smp = 0; smp < numSamples; ++smp) {
                    const float gateSample = data[smp];
                    if (v == 0 && std::abs(gateSample) > std::abs(meterPeak))
                        meterPeak = gateSample;

                    float threshold = baseThreshold;
                    if (thresholdCV != nullptr)
                        threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                    if (v == 0)
                        lastThreshold = threshold;

                    const auto edge = gateTriggers[v].process(gateSample, threshold);
                    if (edge == SchmittTrigger::Edge::Rising) {
                        adsrs[v].noteOn();
                        if (v == 0)
                            ++firedThisBlock;
                    } else if (edge == SchmittTrigger::Edge::Falling) {
                        adsrs[v].noteOff();
                    }
                    data[smp] = adsrs[v].getNextSample();
                }
            }

            for (int ch = voices; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);

            if (auto* vb = getVisualBuffer())
                for (int smp = 0; smp < numSamples; ++smp)
                    vb->pushSample(buffer.getSample(0, smp));
        }

        meterLevel.store(meterPeak, std::memory_order_relaxed);
        effectiveThreshold.store(lastThreshold, std::memory_order_relaxed);
        overThreshold.store(poly ? gateTriggers[0].high : previousActive[0], std::memory_order_relaxed);
        if (firedThisBlock > 0)
            triggerCount.fetch_add(firedThisBlock, std::memory_order_relaxed);
    }

    // processBlock consumes note-on/off to drive midiGateHeld (a MIDI fallback for the Gate
    // input) but never writes to the MIDI buffer.
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::Envelope; }
    juce::String getInputPortLabel(int i) const override {
        return i == 0 ? "Gate" : i == 1 ? "Threshold" : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int) const override { return "Env"; }
    int getVisibleInputPortCount() const override { return 2; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModuleType getModuleType() const override { return ModuleType::ADSR; }

    std::vector<ModulationTarget> getModulationTargets() const override { return {{"Threshold", kThresholdChannel}}; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Gate;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
        } else if (raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::Gate;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        if (raw == kThresholdChannel) {
            p.visibleJackIndex = 1;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    LogicalPort mapOutputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
        } else if (raw == 0) {
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapOutputChannel(raw);
    }

    float getMeterLevel() const override { return meterLevel.load(std::memory_order_relaxed); }
    float getEffectiveThreshold() const override { return effectiveThreshold.load(std::memory_order_relaxed); }
    bool isOverThreshold() const override { return overThreshold.load(std::memory_order_relaxed); }
    int getTriggerCount() const override { return triggerCount.load(std::memory_order_relaxed); }
    ThresholdScale getThresholdScale() const override { return ThresholdScale::Unipolar; }
    juce::String getThresholdParamID() const override { return "gateThreshold"; }
    juce::String getMeterIdleLabel() const override { return "no gate"; }

    static constexpr float getTriggerHysteresis() { return SchmittTrigger::kHysteresis; }

private:
    static constexpr int MAX_VOICES = 8;
    static constexpr int kThresholdChannel = 8;

    void resetMeters() {
        meterLevel.store(0.0f, std::memory_order_relaxed);
        overThreshold.store(false, std::memory_order_relaxed);
    }

    juce::ADSR adsrs[MAX_VOICES];
    juce::ADSR::Parameters adsrParams;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSustain;
    SchmittTrigger gateTriggers[MAX_VOICES];
    bool previousActive[MAX_VOICES] = {};
    bool midiGateHeld = false;
    juce::AudioParameterBool* polyParam = nullptr;

    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* thresholdParam = nullptr;

    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> effectiveThreshold{0.5f};
    std::atomic<bool> overThreshold{false};
    std::atomic<int> triggerCount{0};
};
