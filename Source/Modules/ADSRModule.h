#pragma once

#include "Envelope/EnvelopeGenerator.h"
#include "ModuleBase.h"
#include "SchmittTrigger.h"
#include "ThresholdMeterSource.h"
#include <algorithm>
#include <atomic>
#include <bitset>
#include <cmath>
#include <vector>

class ADSRModule
    : public ModuleBase
    , public ThresholdMeterSource {
public:
    ADSRModule(const juce::String& name = "ADSR")
        : ModuleBase(name, 9, 9) // 8 gate CV per voice + shared Threshold CV; 8 env + silent ch8
    {
        // Times move to [0, 5] with a 0.3 skew (a usable knob at a 1 ms default); the retired
        // minimum-time clamps are gone on purpose -- 0 ms is a real, reachable, click-is-your-
        // choice value, not a bug. See docs/modules.md for why 5.0 stays the ceiling.
        addParameter(attackParam = new juce::AudioParameterFloat(
                         "attack", "Attack", juce::NormalisableRange<float>(0.0f, 5.0f, 0.0f, 0.3f), 0.001f));
        addParameter(decayParam = new juce::AudioParameterFloat(
                         "decay", "Decay", juce::NormalisableRange<float>(0.0f, 5.0f, 0.0f, 0.3f), 1.0f));
        addParameter(sustainParam = new juce::AudioParameterFloat("sustain", "Sustain", 0.0f, 1.0f, 1.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat(
                         "release", "Release", juce::NormalisableRange<float>(0.0f, 5.0f, 0.0f, 0.3f), 0.015f));
        addParameter(holdParam = new juce::AudioParameterFloat(
                         "hold", "Hold", juce::NormalisableRange<float>(0.0f, 5.0f, 0.0f, 0.3f), 0.0f));
        addParameter(attackCurveParam = new juce::AudioParameterFloat(
                         "attackCurve", "Attack Curve", juce::NormalisableRange<float>(-1.0f, 1.0f), -0.3f));
        addParameter(decayCurveParam = new juce::AudioParameterFloat(
                         "decayCurve", "Decay Curve", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.65f));
        addParameter(releaseCurveParam = new juce::AudioParameterFloat(
                         "releaseCurve", "Release Curve", juce::NormalisableRange<float>(-1.0f, 1.0f), 0.65f));
        // `gateThreshold`, not `threshold` / `trigThreshold`: Compressor owns `threshold` as dB,
        // Sample & Hold / Comparator own `trigThreshold` as bipolar CV. ADSR gates are unipolar.
        addParameter(thresholdParam = new juce::AudioParameterFloat("gateThreshold", "Threshold", 0.0f, 1.0f, 0.5f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        for (int v = 0; v < MAX_VOICES; ++v) {
            envelopes[v].setSampleRate(sampleRate);
            envelopes[v].reset();
            gateTriggers[v].reset();
            previousActive[v] = false;
        }
        heldNotes.reset();
        lastTriggeredVoice = 0;
        sustainScratch.assign(static_cast<size_t>(std::max(samplesPerBlock, 1)), sustainParam->get());
        resetMeters();
        effectiveThreshold.store(thresholdParam->get(), std::memory_order_relaxed);
        playheadStage.store(synth::EnvelopeStage::Idle, std::memory_order_relaxed);
        playheadProgress.store(0.0f, std::memory_order_relaxed);
        playheadLevel.store(0.0f, std::memory_order_relaxed);
        // Sustain is the one envelope parameter that is a LEVEL, not a stage time:
        // EnvelopeGenerator reads it fresh every sample (as Decay's live target and as the flat
        // Sustain output), so a per-sample smoother lets automation retarget an in-flight decay
        // or a held note smoothly instead of stepping. Attack/Hold/Decay/Release are stage
        // *times* and are deliberately left unsmoothed -- EnvelopeGenerator turns a time change
        // mid-ramp into a slope change, never a level jump, entirely on its own.
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

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        smoothedSustain.setTargetValue(*sustainParam);
        if (static_cast<int>(sustainScratch.size()) < numSamples)
            sustainScratch.resize(static_cast<size_t>(numSamples));
        for (int smp = 0; smp < numSamples; ++smp)
            sustainScratch[static_cast<size_t>(smp)] = smoothedSustain.getNextValue();

        const bool poly = *polyParam;
        const float baseThreshold = thresholdParam->get();
        const float* thresholdCV = numChannels > kThresholdChannel ? buffer.getReadPointer(kThresholdChannel) : nullptr;

        const float attack = *attackParam;
        const float hold = *holdParam;
        const float decay = *decayParam;
        const float release = *releaseParam;
        const float attackCurve = *attackCurveParam;
        const float decayCurve = *decayCurveParam;
        const float releaseCurve = *releaseCurveParam;

        float meterPeak = 0.0f;
        float lastThreshold = baseThreshold;
        int firedThisBlock = 0;

        if (!poly) {
            const float* gateIn = buffer.getReadPointer(0);
            float* envOut = buffer.getWritePointer(0);

            for (int smp = 0; smp < numSamples; ++smp) {
                bool midiNoteOnThisSample = false;
                for (const auto metadata : midiMessages) {
                    if (metadata.samplePosition != smp)
                        continue;
                    const auto message = metadata.getMessage();
                    if (message.isNoteOn()) {
                        heldNotes.set(static_cast<size_t>(message.getNoteNumber()));
                        // The note-on EVENT drives the retrigger, not an edge in a held/not-held
                        // flag -- a note-off + note-on landing on the same sample (a gapless
                        // mono legato transition) nets out to "still held" on the flag, but must
                        // still re-articulate (FRO110).
                        envelopes[0].noteOn();
                        lastTriggeredVoice = 0;
                        ++firedThisBlock;
                        midiNoteOnThisSample = true;
                    } else if (message.isNoteOff()) {
                        heldNotes.reset(static_cast<size_t>(message.getNoteNumber()));
                    } else if (message.isAllNotesOff() || message.isAllSoundOff()) {
                        heldNotes.reset();
                    }
                }

                const float gateSample = gateIn[smp];
                if (std::abs(gateSample) > std::abs(meterPeak))
                    meterPeak = gateSample;

                float threshold = baseThreshold;
                if (thresholdCV != nullptr)
                    threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                lastThreshold = threshold;

                gateTriggers[0].process(gateSample, threshold);
                // Release only once every held note is gone AND the Gate CV is low -- the OR
                // with the Schmitt trigger's own held state is unchanged from before.
                const bool active = heldNotes.any() || gateTriggers[0].high;
                if (active && !previousActive[0] && !midiNoteOnThisSample) {
                    envelopes[0].noteOn();
                    lastTriggeredVoice = 0;
                    ++firedThisBlock;
                } else if (!active && previousActive[0]) {
                    envelopes[0].noteOff();
                }
                previousActive[0] = active;

                envOut[smp] = envelopes[0].getNextSample(
                    makeParameters(attack, hold, decay, sustainScratch[static_cast<size_t>(smp)], release, attackCurve,
                                   decayCurve, releaseCurve));
            }

            for (const auto metadata : midiMessages) {
                if (metadata.samplePosition < numSamples)
                    continue;
                const auto message = metadata.getMessage();
                if (message.isNoteOn())
                    heldNotes.set(static_cast<size_t>(message.getNoteNumber()));
                else if (message.isNoteOff())
                    heldNotes.reset(static_cast<size_t>(message.getNoteNumber()));
                else if (message.isAllNotesOff() || message.isAllSoundOff())
                    heldNotes.reset();
            }

            for (int ch = 1; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);

            if (auto* vb = getVisualBuffer())
                for (int smp = 0; smp < numSamples; ++smp)
                    vb->pushSample(envOut[smp]);
        } else {
            float* voiceData[MAX_VOICES] = {};
            const int voices = std::min(MAX_VOICES, numChannels);
            for (int v = 0; v < voices; ++v)
                voiceData[v] = buffer.getWritePointer(v);

            for (int smp = 0; smp < numSamples; ++smp) {
                float threshold = baseThreshold;
                if (thresholdCV != nullptr)
                    threshold = juce::jlimit(0.0f, 1.0f, threshold + thresholdCV[smp]);
                lastThreshold = threshold;

                const synth::EnvelopeParameters ep =
                    makeParameters(attack, hold, decay, sustainScratch[static_cast<size_t>(smp)], release, attackCurve,
                                   decayCurve, releaseCurve);

                for (int v = 0; v < voices; ++v) {
                    const float gateSample = voiceData[v][smp];
                    if (v == 0 && std::abs(gateSample) > std::abs(meterPeak))
                        meterPeak = gateSample;

                    const auto edge = gateTriggers[v].process(gateSample, threshold);
                    if (edge == SchmittTrigger::Edge::Rising) {
                        envelopes[v].noteOn();
                        lastTriggeredVoice = v;
                        if (v == 0)
                            ++firedThisBlock;
                    } else if (edge == SchmittTrigger::Edge::Falling) {
                        envelopes[v].noteOff();
                    }
                    voiceData[v][smp] = envelopes[v].getNextSample(ep);
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

        // Lock-free UI playhead for the most recently (re)triggered voice, written once per
        // block -- not per sample -- for a future graph-editor overlay.
        const int voiceForPlayhead = juce::jlimit(0, MAX_VOICES - 1, lastTriggeredVoice);
        playheadStage.store(envelopes[voiceForPlayhead].getStage(), std::memory_order_relaxed);
        playheadProgress.store(envelopes[voiceForPlayhead].getProgress(), std::memory_order_relaxed);
        playheadLevel.store(envelopes[voiceForPlayhead].getLevel(), std::memory_order_relaxed);
    }

    // processBlock consumes note-on/off to drive heldNotes (a MIDI fallback for the Gate
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

    // Lock-free UI playhead accessors (FRO110): the stage/progress/level of the most recently
    // (re)triggered voice, refreshed once per block. Not yet consumed by any UI.
    synth::EnvelopeStage getPlayheadStage() const noexcept { return playheadStage.load(std::memory_order_relaxed); }
    float getPlayheadProgress() const noexcept { return playheadProgress.load(std::memory_order_relaxed); }
    float getPlayheadLevel() const noexcept { return playheadLevel.load(std::memory_order_relaxed); }

private:
    static constexpr int MAX_VOICES = 8;
    static constexpr int kThresholdChannel = 8;

    static synth::EnvelopeParameters makeParameters(float attack, float hold, float decay, float sustain, float release,
                                                    float attackCurve, float decayCurve, float releaseCurve) {
        synth::EnvelopeParameters ep;
        ep.attack = attack;
        ep.hold = hold;
        ep.decay = decay;
        ep.sustain = sustain;
        ep.release = release;
        ep.attackCurve = attackCurve;
        ep.decayCurve = decayCurve;
        ep.releaseCurve = releaseCurve;
        return ep;
    }

    void resetMeters() {
        meterLevel.store(0.0f, std::memory_order_relaxed);
        overThreshold.store(false, std::memory_order_relaxed);
    }

    synth::EnvelopeGenerator envelopes[MAX_VOICES];
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSustain;
    std::vector<float> sustainScratch;
    SchmittTrigger gateTriggers[MAX_VOICES];
    bool previousActive[MAX_VOICES] = {};
    std::bitset<128> heldNotes; // keyed by MIDI note number only, channel-agnostic
    int lastTriggeredVoice = 0;
    juce::AudioParameterBool* polyParam = nullptr;

    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* decayParam = nullptr;
    juce::AudioParameterFloat* sustainParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* holdParam = nullptr;
    juce::AudioParameterFloat* attackCurveParam = nullptr;
    juce::AudioParameterFloat* decayCurveParam = nullptr;
    juce::AudioParameterFloat* releaseCurveParam = nullptr;
    juce::AudioParameterFloat* thresholdParam = nullptr;

    std::atomic<float> meterLevel{0.0f};
    std::atomic<float> effectiveThreshold{0.5f};
    std::atomic<bool> overThreshold{false};
    std::atomic<int> triggerCount{0};
    std::atomic<synth::EnvelopeStage> playheadStage{synth::EnvelopeStage::Idle};
    std::atomic<float> playheadProgress{0.0f};
    std::atomic<float> playheadLevel{0.0f};
};
