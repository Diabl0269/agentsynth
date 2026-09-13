#pragma once

#include "../ModuleBase.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

// Standard noise gate — Threshold/Attack/Hold/Release/Range, stereo-linked. See
// docs/fx_modules.md § Gate Module for the full spec (hysteresis rationale, why the detector is
// linked, v1 scope). No sidechain input in v1 (see docs/fx_modules.md § Gate Module).
class GateModule : public ModuleBase {
public:
    // The gate opens once the linked envelope reaches Threshold and closes only once it falls
    // this many dB BELOW Threshold — a Schmitt trigger, so a signal hovering right at Threshold
    // does not chatter the gate open/closed every few samples. A few dB is standard practice for
    // hardware/software noise gates; kept as a named constant rather than a user parameter (v1
    // scope, documented in docs/fx_modules.md).
    static constexpr float kGateHysteresisDb = 3.0f;

    GateModule()
        : ModuleBase("Gate", 2, 2) {
        addParameter(thresholdParam =
                         new juce::AudioParameterFloat("threshold", "Threshold (dB)", -80.0f, 0.0f, -40.0f));
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack (ms)", 0.1f, 200.0f, 2.0f));
        addParameter(holdParam = new juce::AudioParameterFloat("hold", "Hold (ms)", 0.0f, 500.0f, 10.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 5.0f, 2000.0f, 150.0f));
        // The gain applied when fully closed (the floor). -80 dB default is near-silent without
        // being a hard mute, so a gate left at default still reports honestly if something did
        // leak through underneath it. -20 dB, for example, leaves 0.1 linear amplitude through —
        // deliberately not silence; see GateModuleTest.RangeFloorIsNotSilence.
        addParameter(rangeParam = new juce::AudioParameterFloat("range", "Range (dB)", -80.0f, 0.0f, -80.0f));
        // Every audio-output module needs a level control (docs/fx_modules.md § Output Level);
        // Range is the closed-state floor, not a general trim, so it cannot double as this the
        // way Compressor's makeupGain / Limiter's inputGain do — adopt the shared stage instead.
        addOutputLevelParameter();
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        sampleRate_ = sampleRate;

        smoothedThreshold.reset(sampleRate, 0.01);
        smoothedRange.reset(sampleRate, 0.01);
        smoothedThreshold.setCurrentAndTargetValue(*thresholdParam);
        smoothedRange.setCurrentAndTargetValue(*rangeParam);

        // Snap to the closed steady state — a fresh gate that has seen no signal yet is closed,
        // not open, and starting exactly at the Range floor keeps a static render deterministic.
        detectorOpen = false;
        holdCounter = 0;
        currentGainLin = juce::Decibels::decibelsToGain(smoothedRange.getCurrentValue());

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

        const int numSamples = buffer.getNumSamples();
        if (numSamples == 0 || buffer.getNumChannels() < 2)
            return;

        smoothedThreshold.setTargetValue(*thresholdParam);
        smoothedRange.setTargetValue(*rangeParam);

        // Threshold and Range are levels that feed the gain computer directly (opening/closing
        // decision, closed-state floor), so — like Compressor's Threshold/Ratio — they are
        // smoothed a block at a time. Attack/Hold/Release are deliberately NOT smoothed: Attack
        // and Release are detector/ramp time constants (stepping one changes how fast the gate
        // moves, never the currently-applied gain — same reasoning as Compressor's own
        // Attack/Release), and Hold is consulted only at the discrete "signal just dropped below
        // the close level" event, the same category as a sequencer's gate length.
        const float openThreshLin = juce::Decibels::decibelsToGain(smoothedThreshold.getCurrentValue());
        const float closeThreshLin =
            juce::Decibels::decibelsToGain(smoothedThreshold.getCurrentValue() - kGateHysteresisDb);
        const float rangeGainLin = juce::Decibels::decibelsToGain(smoothedRange.getCurrentValue());

        const int attackSamples = juce::jmax(1, (int)(attackParam->get() * 0.001 * sampleRate_));
        const int releaseSamples = juce::jmax(1, (int)(releaseParam->get() * 0.001 * sampleRate_));
        const int holdSamples = juce::jmax(0, (int)(holdParam->get() * 0.001 * sampleRate_));

        // Attack/Release ramp linearly across the full open<->closed span, so "Attack" always
        // means "time to fully open from fully closed" regardless of where Range currently sits.
        const float span = 1.0f - rangeGainLin;
        const float attackStep = span / (float)attackSamples;
        const float releaseStep = span / (float)releaseSamples;

        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i) {
            // Stereo-linked detector: ONE gain computer driven by max(|L|,|R|), so the stereo
            // image never shifts (docs/fx_modules.md § Gate Module).
            const float envelope = std::max(std::abs(left[i]), std::abs(right[i]));

            if (!detectorOpen) {
                if (envelope >= openThreshLin) {
                    detectorOpen = true;
                    holdCounter = holdSamples;
                }
            } else if (envelope < closeThreshLin) {
                // Below the close level: keep counting down the hold window before releasing.
                if (holdCounter > 0)
                    --holdCounter;
                else
                    detectorOpen = false;
            } else {
                // Still at or above the close level (even if below Threshold — that's the
                // hysteresis gap): the gate stays open and the hold window resets, so Hold always
                // measures from the moment the signal actually drops below Threshold - hysteresis.
                holdCounter = holdSamples;
            }

            const float target = detectorOpen ? 1.0f : rangeGainLin;
            if (currentGainLin < target)
                currentGainLin = std::min(target, currentGainLin + attackStep);
            else if (currentGainLin > target)
                currentGainLin = std::max(target, currentGainLin - releaseStep);

            left[i] *= currentGainLin;
            right[i] *= currentGainLin;
        }

        smoothedThreshold.skip(numSamples);
        smoothedRange.skip(numSamples);

        applyOutputLevel(buffer, 2);
    }

    juce::String getInputPortLabel(int i) const override { return stereoInputLabel(i, 0, nullptr); }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(0); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 0); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // No sidechain input in v1 — out of scope; see docs/fx_modules.md § Gate Module.
    std::vector<ModulationTarget> getModulationTargets() const override { return {}; }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Gate; }

private:
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedThreshold;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedRange;

    juce::AudioParameterFloat* thresholdParam;
    juce::AudioParameterFloat* attackParam;
    juce::AudioParameterFloat* holdParam;
    juce::AudioParameterFloat* releaseParam;
    juce::AudioParameterFloat* rangeParam;

    double sampleRate_ = 44100.0;
    bool detectorOpen = false;
    int holdCounter = 0;
    float currentGainLin = 0.0f;
};
