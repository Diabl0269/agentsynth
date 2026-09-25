#pragma once

#include "ModuleBase.h"
#include <juce_core/juce_core.h>
#include <random>

/** Low-frequency oscillator / CV source.
 *
 *  Channel layout (mono):
 *    In  ch0 Rate CV | ch1 Level CV | ch2 Glide CV
 *    Out ch0 CV      | ch1-2 silent pass-throughs (prevent AudioProcessorGraph buffer aliasing,
 *                       docs/modules/poly-channel-layout.md)
 *
 *  ch0 carries the Rate CV input on the way in and the LFO's own CV output on the way out — read
 *  before overwrite, the same shared-channel convention as Oscillator/Wavetable ch0 and
 *  Sample & Hold ch0.
 */
class LFOModule : public ModuleBase {
public:
    // Declared output count must be >= the highest CV input channel this module reads (Glide,
    // ch2), per docs/modules/poly-channel-layout.md, or JUCE aliases ch1/ch2 onto ch0's buffer.
    static constexpr int kNumInputs = 3;
    static constexpr int kNumOutputs = 3;

    LFOModule()
        : ModuleBase("LFO", kNumInputs, kNumOutputs) { // Rate/Level/Glide CV in, 1 Control Output
        // Enable visual buffer for scope display
        enableVisualBuffer(true);

        // Shape
        juce::StringArray shapes({"Sine", "Triangle", "Sawtooth", "Square", "S&H"});
        addParameter(shapeParam = new juce::AudioParameterChoice("shape", "Shape", shapes, 0));

        // Mode: Hz (false) / Sync (true)
        addParameter(modeParam = new juce::AudioParameterBool("mode", "Sync", true));

        // Bipolar: Unipolar (false) / Bipolar (true)
        addParameter(bipolarParam = new juce::AudioParameterBool("bipolar", "Bipolar", true));

        // Rate Hz
        addParameter(rateHzParam = new juce::AudioParameterFloat(
                         "rateHz", "Rate (Hz)", juce::NormalisableRange<float>(0.01f, 20.0f, 0.01f, 0.5f), 1.0f));

        // Rate Sync
        juce::StringArray syncs({"1/1", "1/2", "1/4", "1/8", "1/16", "1/32"});
        addParameter(rateSyncParam = new juce::AudioParameterChoice("rateSync", "Sync Rate", syncs, 2)); // Default 1/4

        // Retrig
        addParameter(retrigParam = new juce::AudioParameterBool("retrig", "Retrig", false));

        // Output Level
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));

        // Glide (S&H only)
        addParameter(glideParam = new juce::AudioParameterFloat("glide", "Glide", 0.0f, 1.0f, 0.0f));
        addMuteParameter();
    }

    void prepareToPlay(double sampleRate, int /*samplesPerBlock*/) override {
        currentSampleRate = sampleRate;
        shSmoother.reset(sampleRate, 0.05); // 50ms default ramp for smooth glide
        // Level scales the emitted CV directly, so an automated step steps every destination
        // downstream. Snapped at prepare so a static render is unchanged.
        smoothedLevel.reset(sampleRate, 0.01);
        smoothedLevel.setCurrentAndTargetValue(levelParam->get());
    }

    void releaseResources() override {}

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        // Two separate branches, per docs/architecture/module-base.md#bypassmute-contract. A pure
        // source has no dry audio path to pass through, so bypass clears here rather than
        // returning early — but it stays its own branch so the two cases can never be conflated.
        if (isBypassed()) {
            buffer.clear();
            return;
        }

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (buffer.getNumSamples() == 0 || buffer.getNumChannels() == 0)
            return;

        // Rate/Level/Glide CV, read once per block (docs/modules/modulation.md#cv-in-normalised-units).
        // ch0 must be read before the sample loop below overwrites it with the CV output.
        const float rateCv = blockCV(buffer, 0);
        const float levelCv = blockCV(buffer, 1);
        const float glideCv = blockCV(buffer, 2);

        auto* channelData0 = buffer.getWritePointer(0);

        // Process MIDI for Retrig
        if (retrigParam->get()) {
            for (const auto metadata : midiMessages) {
                auto msg = metadata.getMessage();
                if (msg.isNoteOn()) {
                    phase = 0.0f;
                }
            }
        }

        float rate = 0.0f;
        if (!modeParam->get()) { // Hz
            // Rate CV only applies in Hz mode: it moves rateHz in its own (skewed) normalised
            // range, same convention as every other block-rate CV jack.
            rate = modulateNormalised(*rateHzParam, rateHzParam->get(), rateCv);
        } else { // Sync
            // Sync mode's rate is a tempo division, not a knob value — there is nothing for Rate
            // CV to move against, so it is deliberately ignored here.
            // Calculate rate from BPM
            // For now, assume 120 if no PlayHead or BPM is available.
            // Ideally get from PlayHead.
            double bpm = 120.0;
            if (auto* ph = getPlayHead()) {
                if (auto pos = ph->getPosition()) {
                    if (pos->getBpm().hasValue())
                        bpm = *pos->getBpm();
                }
            }

            float subdivision = 4.0f; // 1/1 = 4 quarter notes
            int syncIndex = rateSyncParam->getIndex();
            // "1/1", "1/2", "1/4", "1/8", "1/16", "1/32"
            // 1/1 = 4 beats
            // 1/2 = 2 beats
            // 1/4 = 1 beat
            // 1/8 = 0.5 beats
            switch (syncIndex) {
            case 0:
                subdivision = 4.0f;
                break; // 1/1
            case 1:
                subdivision = 2.0f;
                break; // 1/2
            case 2:
                subdivision = 1.0f;
                break; // 1/4
            case 3:
                subdivision = 0.5f;
                break; // 1/8
            case 4:
                subdivision = 0.25f;
                break; // 1/16
            case 5:
                subdivision = 0.125f;
                break; // 1/32
            }

            // Frequency = BPM / 60 / subdivision_in_beats?
            // rate in Hz = 1 / (time associated with subdivision)
            // time for 1 beat = 60 / BPM
            // time for subdivision = (60 / BPM) * subdivision (where 1/4 is 1 beat)
            // Wait, standard musical notation:
            // 1/4 note at 120 BPM: 60/120 = 0.5s. Freq = 2Hz.
            // subdivision var above logic:
            // index 2 (1/4) -> 1.0 beats.
            // length in seconds = (60.0 / bpm) * subdivision_beats
            // Freq = 1.0 / length
            rate = 1.0f / ((60.0 / bpm) * subdivision);
        }

        // Rate is a frequency: stepping it changes the phase increment while the phase itself
        // stays continuous, so it cannot click. Deliberately not smoothed.
        float phaseIncrement = rate / (float)currentSampleRate;
        smoothedLevel.setTargetValue(modulateNormalised(*levelParam, levelParam->get(), levelCv));
        int shape = shapeParam->getIndex();

        for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
            const float level = smoothedLevel.getNextValue();
            float currentSample = 0.0f;

            switch (shape) {
            case 0: // Sine
                currentSample = std::sin(phase * juce::MathConstants<float>::twoPi);
                break;
            case 1: // Triangle
                currentSample = 2.0f * std::abs(2.0f * (phase - std::floor(phase + 0.5f))) - 1.0f;
                break;
            case 2: // Sawtooth
                currentSample = 2.0f * (phase - 0.5f);
                break;
            case 3: // Square
                currentSample = (phase < 0.5f) ? 1.0f : -1.0f;
                break;
            case 4: // S&H
                // The value is managed by shSmoother
                currentSample = shSmoother.getNextValue();
                break;
            }

            // Advance phase
            phase += phaseIncrement;

            // Wrap and handle S&H trigger
            if (phase >= 1.0f) {
                phase -= 1.0f;
                if (shape == 4) {
                    // Trigger new random value
                    lastRandomSample = (random.nextFloat() * 2.0f) - 1.0f;

                    // Glide is read only at this S&H step edge, where it re-times the ramp to the
                    // next held value — a discrete event, nothing to smooth. glideCv was captured
                    // once per block above, the same convention as every other CV jack.
                    float glideValue = modulateNormalised(*glideParam, glideParam->get(), glideCv);
                    if (glideValue <= 0.0f) {
                        shSmoother.setCurrentAndTargetValue(lastRandomSample);
                    } else {
                        // Map 0..1 to 0..0.5s glide time? Or just let it be linear.
                        // Adjust smoother time based on glide param
                        shSmoother.reset(currentSampleRate, juce::jmax(0.001f, glideValue * 0.5f));
                        shSmoother.setTargetValue(lastRandomSample);
                    }
                }
            }

            // If we just switched to S&H or something, ensure initialized?
            // lastRandomSample init to 0.

            if (!bipolarParam->get()) {
                // Unipolar conversion: map [-1, 1] to [0, 1]
                currentSample = (currentSample + 1.0f) * 0.5f;
            }

            float outputSample = currentSample * level;
            channelData0[sample] = outputSample;

            // Push to visual buffer for scope display
            if (auto* vb = getVisualBuffer())
                vb->pushSample(outputSample);
        }

        // ch1/ch2 (Level/Glide CV in) are silent pass-throughs on the way out, same as Sample &
        // Hold's ch1-6 — clearing them stops the raw CV values leaking downstream as output.
        for (int ch = 1; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
    }

    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::LFO; }
    bool isModSourceBipolar() const override { return bipolarParam->get(); }
    juce::String getOutputPortLabel(int) const override { return "CV"; }
    ModuleType getModuleType() const override { return ModuleType::LFO; }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Rate", "Level", "Glide"};
        return (i >= 0 && i < kNumInputs) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }

    // Only ch0 (CV out) is visible; ch1/ch2 are silent pass-throughs (see class comment).
    int getVisibleOutputPortCount() const override { return 1; }

    // Every raw input channel this module declares must be claimed here, or an unclaimed one
    // below getVisibleInputPortCount() becomes a phantom poly-group head
    // (docs/modules/modulation.md#logical-port-api).
    LogicalPort mapInputChannel(int raw) const override {
        if (raw >= 0 && raw < kNumInputs) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = PortRole::ModCV;
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
        return ModuleBase::mapOutputChannel(raw); // ch1/ch2: hidden pass-throughs, not a jack
    }

    // Every continuous parameter gets a CV jack
    // (docs/modules/modulation.md#every-continuous-parameter-is-a-target). paramId binds each
    // jack to its knob; "Rate" vs the knob name "Rate (Hz)" would not match without it.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Rate", 0, "rateHz"}, {"Level", 1, "level"}, {"Glide", 2, "glide"}};
    }

private:
    juce::AudioParameterChoice* shapeParam;
    juce::AudioParameterBool* modeParam; // Sync (true)
    juce::AudioParameterFloat* rateHzParam;
    juce::AudioParameterChoice* rateSyncParam;
    juce::AudioParameterBool* bipolarParam;
    juce::AudioParameterBool* retrigParam;
    juce::AudioParameterFloat* levelParam;
    juce::AudioParameterFloat* glideParam;

    float phase = 0.0f;
    double currentSampleRate = 44100.0;

    float lastRandomSample = 0.0f;
    juce::Random random;
    juce::LinearSmoothedValue<float> shSmoother;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedLevel;
};
