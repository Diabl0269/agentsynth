#pragma once

#include "ModuleBase.h"
#include <atomic>
#include <cmath>
#include <juce_core/juce_core.h>

/** Sample & Hold / Randomizer.
 *
 *  Captures the value of a source signal on every clock edge and holds it until the next
 *  one, producing the stepped CV behind generative sequences and "R2D2" style bleeps.
 *
 *  The source is either the Signal input or an internal white-noise generator (`Source`),
 *  and the clock is either the Trigger input or a free-running internal oscillator
 *  (`Clock`). Both are explicit choices rather than "is anything patched in?" heuristics —
 *  a gate signal sits at 0 most of the time, so activity detection would misfire.
 *
 *  `Mode` selects classic sample & hold (latch one value per rising edge) or track & hold
 *  (follow the source while the gate is high, freeze when it falls).
 *
 *  Channel layout (mono):
 *    In  ch0 Signal | ch1 Trigger | ch2 Rate CV | ch3 Slew CV | ch4 Level CV | ch5 Offset CV
 *    Out ch0 CV     | ch1-5 silent pass-throughs (prevent AudioProcessorGraph buffer aliasing)
 */
class SampleHoldModule : public ModuleBase {
public:
    SampleHoldModule()
        : ModuleBase("Sample & Hold", 6, 6) {
        addParameter(sourceParam = new juce::AudioParameterChoice("source", "Source", {"Input", "Random"}, 1));
        // `holdMode`, not `mode`: LFOModule already declares a boolean `mode`, and the AI patch
        // schema constrains choice parameters globally by id. Reusing `mode` would publish an
        // enum of ["Sample", "Track"] that forbids the LFO's own legal boolean value.
        addParameter(modeParam = new juce::AudioParameterChoice("holdMode", "Mode", {"Sample", "Track"}, 0));
        addParameter(clockParam = new juce::AudioParameterChoice("clock", "Clock", {"Internal", "External"}, 0));
        addParameter(
            rateParam = new juce::AudioParameterFloat(
                "rate", "Rate (Hz)", juce::NormalisableRange<float>(kMinRateHz, kMaxRateHz, 0.01f, 0.5f), 8.0f));
        addParameter(slewParam = new juce::AudioParameterFloat("slew", "Slew", 0.0f, 1.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(offsetParam = new juce::AudioParameterFloat("offset", "Offset", -1.0f, 1.0f, 0.0f));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        // Start "past" the end of a cycle so the internal clock latches a value on the very
        // first sample instead of sitting at 0 until the first wrap.
        phase = 1.0f;
        previousTriggerHigh = false;
        heldValue = 0.0f;
        currentValue = 0.0f;
        cachedSlew = -1.0f; // force slew coefficient recompute on the next block
        slewCoeff = 0.0f;
        lastValue.store(0.0f, std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        if (isBypassed()) {
            // Dry pass-through on ch0; clear trigger + CV channels so they don't leak downstream.
            for (int ch = 1; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            lastValue.store(0.0f, std::memory_order_relaxed);
            return;
        }

        if (isMuted()) {
            buffer.clear();
            lastValue.store(0.0f, std::memory_order_relaxed);
            return;
        }

        const bool useInternalClock = clockParam->getIndex() == 0;
        const bool sampleFromInput = sourceParam->getIndex() == 0;
        const bool trackMode = modeParam->getIndex() == 1;

        const float baseRate = rateParam->get();
        const float baseSlew = slewParam->get();
        const float baseLevel = levelParam->get();
        const float baseOffset = offsetParam->get();

        const float* triggerIn = numChannels > 1 ? buffer.getReadPointer(1) : nullptr;
        const float* rateCV = numChannels > 2 ? buffer.getReadPointer(2) : nullptr;
        const float* slewCV = numChannels > 3 ? buffer.getReadPointer(3) : nullptr;
        const float* levelCV = numChannels > 4 ? buffer.getReadPointer(4) : nullptr;
        const float* offsetCV = numChannels > 5 ? buffer.getReadPointer(5) : nullptr;

        // ch0 carries the Signal input on the way in and the CV output on the way out.
        // Each iteration reads out[s] before overwriting it, so the aliasing is safe.
        float* out = buffer.getWritePointer(0);

        for (int s = 0; s < numSamples; ++s) {
            const float sourceSample = sampleFromInput ? out[s] : ((random.nextFloat() * 2.0f) - 1.0f);

            bool captureNow = false; // Sample mode: latch a new value on this sample
            bool tracking = false;   // Track mode: follow the source on this sample

            if (useInternalClock) {
                float rate = baseRate;
                if (rateCV != nullptr) {
                    // +/- 4 octaves of exponential CV around the knob, following the project's
                    // "map raw CV to the full native range" convention.
                    rate = baseRate * std::exp2(juce::jlimit(-1.0f, 1.0f, rateCV[s]) * 4.0f);
                }
                rate = juce::jlimit(kMinRateHz, kMaxRateHz, rate);

                phase += rate / (float)currentSampleRate;
                if (phase >= 1.0f) {
                    phase -= std::floor(phase);
                    captureNow = true;
                }
                tracking = phase < 0.5f; // 50% duty internal gate
            } else if (triggerIn != nullptr) {
                const bool high = triggerIn[s] > kTriggerThreshold;
                captureNow = high && !previousTriggerHigh;
                previousTriggerHigh = high;
                tracking = high;
            }

            if (trackMode ? tracking : captureNow)
                heldValue = sourceSample;

            float slew = baseSlew;
            if (slewCV != nullptr)
                slew = juce::jlimit(0.0f, 1.0f, slew + slewCV[s]);

            // One-pole lag toward the held value. The coefficient only depends on `slew`, so
            // recompute it when that actually moves rather than calling exp() every sample.
            if (slew != cachedSlew) {
                cachedSlew = slew;
                const float tau = slew * kMaxSlewSeconds;
                slewCoeff = tau > 0.0f ? std::exp(-1.0f / (tau * (float)currentSampleRate)) : 0.0f;
            }
            currentValue = heldValue + (currentValue - heldValue) * slewCoeff;

            float level = baseLevel;
            if (levelCV != nullptr)
                level = juce::jlimit(0.0f, 1.0f, level + levelCV[s]);

            float offset = baseOffset;
            if (offsetCV != nullptr)
                offset = juce::jlimit(-1.0f, 1.0f, offset + offsetCV[s]);

            out[s] = juce::jlimit(-1.0f, 1.0f, currentValue * level + offset);
        }

        // Silence the trigger + CV channels so they don't leak downstream as output.
        for (int ch = 1; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        lastValue.store(out[numSamples - 1], std::memory_order_relaxed);

        if (auto* vb = getVisualBuffer()) {
            for (int s = 0; s < numSamples; ++s)
                vb->pushSample(out[s]);
        }
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Rate", 2}, {"Slew", 3}, {"Level", 4}, {"Offset", 5}};
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Signal", "Trigger", "Rate", "Slew", "Level", "Offset"};
        return (i >= 0 && i < 6) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int) const override { return "CV"; }

    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::LFO; }
    ModuleType getModuleType() const override { return ModuleType::SampleHold; }

    LogicalPort mapInputChannel(int raw) const override {
        if (raw >= 0 && raw < 6) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = (raw == 0) ? PortRole::Audio : (raw == 1 ? PortRole::Gate : PortRole::ModCV);
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    LogicalPort mapOutputChannel(int raw) const override {
        if (raw == 0) {
            LogicalPort p;
            p.visibleJackIndex = 0;
            p.role = PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapOutputChannel(raw);
    }

    /** Last CV value emitted on ch0 — for UI visualisation and tests. */
    float getLastValue() const { return lastValue.load(std::memory_order_relaxed); }

private:
    static constexpr float kMinRateHz = 0.1f;
    static constexpr float kMaxRateHz = 50.0f;
    static constexpr float kTriggerThreshold = 0.5f;
    static constexpr float kMaxSlewSeconds = 0.5f;

    juce::AudioParameterChoice* sourceParam = nullptr;
    juce::AudioParameterChoice* modeParam = nullptr;
    juce::AudioParameterChoice* clockParam = nullptr;
    juce::AudioParameterFloat* rateParam = nullptr;
    juce::AudioParameterFloat* slewParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterFloat* offsetParam = nullptr;

    double currentSampleRate = 44100.0;
    float phase = 1.0f;
    bool previousTriggerHigh = false;
    float heldValue = 0.0f;
    float currentValue = 0.0f;
    float cachedSlew = -1.0f;
    float slewCoeff = 0.0f;
    juce::Random random;
    std::atomic<float> lastValue{0.0f};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SampleHoldModule)
};
