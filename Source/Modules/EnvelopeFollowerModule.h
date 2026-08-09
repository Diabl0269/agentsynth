#pragma once

#include "ModuleBase.h"
#include <cmath>

/**
    Envelope Follower — tracks the amplitude contour of an audio input and emits it as
    unipolar [0, 1] modulation CV. Useful for sidechain-style ducking (follow a drum bus,
    modulate a VCA) and dynamic auto-wah (follow a signal, modulate a Filter cutoff).

    This is a *detector*, not an envelope generator: it has no gate input and no
    decay/sustain stages. ADSR remains the generator; the two are deliberately separate
    modules because their port roles (Gate vs Audio), parameter units (seconds vs
    milliseconds) and bypass semantics all differ.

    Channel layout (mono — this module has no poly mode):
      inputs   0 = Audio, 1 = Attack CV, 2 = Release CV, 3 = Sensitivity CV
      outputs  0 = Env CV, 1-3 = silent

    Output channels 1-3 exist only to satisfy the graph rule that a module must declare at
    least as many outputs as the highest CV input channel it reads, otherwise JUCE's
    AudioProcessorGraph may alias those input channels onto another node's output buffer.
*/
class EnvelopeFollowerModule : public ModuleBase {
public:
    EnvelopeFollowerModule()
        : ModuleBase("Envelope Follower", 4, 4) {
        addParameter(attackParam = new juce::AudioParameterFloat("attack", "Attack (ms)", 0.1f, 200.0f, 10.0f));
        addParameter(releaseParam = new juce::AudioParameterFloat("release", "Release (ms)", 1.0f, 2000.0f, 150.0f));
        // Deliberately a unit-interval [0,1] control rather than a raw gain or a dB value:
        // AIStateMapper::applyParamsToProcessor treats an in-[0,1] value on a wider range as a
        // normalized value and rescales it, so a non-unit "gain" would silently misread AI
        // patches. Mapped internally to 0.25x-4.0x detector gain.
        addParameter(sensitivityParam = new juce::AudioParameterFloat("sensitivity", "Sensitivity", 0.0f, 1.0f, 0.5f));
        addParameter(detectionParam = new juce::AudioParameterChoice("detection", "Detection", {"Peak", "RMS"}, 0));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        detector.reset();
        smoothedSensitivity.reset(currentSampleRate, 0.02);
        smoothedSensitivity.setCurrentAndTargetValue(*sensitivityParam);
        currentEnvelope.store(0.0f, std::memory_order_relaxed);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        if (numSamples == 0 || numChannels == 0)
            return;

        // Bypass/mute contract — both branches clear, and that is deliberate. The usual
        // "bypass = dry pass-through" rule exists so a bypassed module does not break the
        // audio chain, but this module has an audio *input* and no audio *output*: out ch0
        // is Env CV. Passing the dry signal through would push audio-rate samples into a CV
        // destination, which is worse than emitting no modulation. Same reasoning as the
        // documented pure-source exception (Oscillator, Poly MIDI) — there is no dry path.
        // Kept as two separate branches so the intent is explicit, never a fused condition.
        if (isBypassed()) {
            buffer.clear();
            resetDetector();
            return;
        }
        if (isMuted()) {
            buffer.clear();
            resetDetector();
            return;
        }

        const float* audioIn = buffer.getReadPointer(0);
        const float* attackCV = readPointerIfActive(buffer, 1, numSamples);
        const float* releaseCV = readPointerIfActive(buffer, 2, numSamples);
        const float* sensitivityCV = readPointerIfActive(buffer, 3, numSamples);

        // Attack/release are time constants, not signal levels: modulating them at block rate
        // costs one exp() per block instead of one per sample and cannot zipper the output
        // level (it only changes how fast the follower tracks).
        const float attackMs = modulated(*attackParam, attackCV, attackParam->getNormalisableRange());
        const float releaseMs = modulated(*releaseParam, releaseCV, releaseParam->getNormalisableRange());
        const float attackCoeff = timeConstantToCoeff(attackMs);
        const float releaseCoeff = timeConstantToCoeff(releaseMs);

        const bool rms = detectionParam->getIndex() == 1;
        smoothedSensitivity.setTargetValue(*sensitivityParam);

        // Env CV is written to ch0, which is also the audio input — safe because each sample
        // is read before the same index is written. The CV inputs on ch1-3 are read here and
        // only cleared after the loop, so clearing cannot race the reads above.
        float* out = buffer.getWritePointer(0);

        for (int i = 0; i < numSamples; ++i) {
            const float in = audioIn[i];
            const float rectified = rms ? in * in : std::abs(in);
            float env = detector.process(rectified, attackCoeff, releaseCoeff);
            if (rms)
                env = std::sqrt(std::max(0.0f, env));

            float sensitivity = smoothedSensitivity.getNextValue();
            if (sensitivityCV != nullptr)
                sensitivity = juce::jlimit(0.0f, 1.0f, sensitivity + sensitivityCV[i]);

            out[i] = juce::jlimit(0.0f, 1.0f, env * sensitivityToGain(sensitivity));
        }

        for (int ch = 1; ch < getTotalNumOutputChannels() && ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        currentEnvelope.store(out[numSamples - 1], std::memory_order_relaxed);

        if (auto* vb = getVisualBuffer())
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(out[i]);
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Attack", 1}, {"Release", 2}, {"Sensitivity", 3}};
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Audio", "Attack", "Release", "Sensitivity"};
        return (i >= 0 && i < 4) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }

    juce::String getOutputPortLabel(int) const override { return "Env"; }
    int getVisibleInputPortCount() const override { return 4; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Envelope; }
    ModuleType getModuleType() const override { return ModuleType::EnvelopeFollower; }

    LogicalPort mapInputChannel(int raw) const override {
        if (raw >= 0 && raw <= 3) {
            LogicalPort p;
            p.visibleJackIndex = raw;
            p.role = (raw == 0) ? PortRole::Audio : PortRole::ModCV;
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

    /** Last envelope value emitted this block, for meters/tests. Safe to read off-thread. */
    float getCurrentEnvelope() const { return currentEnvelope.load(std::memory_order_relaxed); }

    /** Maps the [0,1] Sensitivity control onto its 0.25x-4.0x detector gain. */
    static float sensitivityToGain(float sensitivity) {
        return std::exp2((juce::jlimit(0.0f, 1.0f, sensitivity) - 0.5f) * 4.0f);
    }

private:
    // A rectify-and-smooth amplitude detector: a one-pole follower with separate rise and
    // fall coefficients. Self-contained so it can be lifted into a shared header verbatim
    // if the Compressor ever grows a sidechain input.
    struct Detector {
        float env = 0.0f;

        void reset() { env = 0.0f; }

        float process(float rectified, float attackCoeff, float releaseCoeff) {
            const float coeff = (rectified > env) ? attackCoeff : releaseCoeff;
            env = rectified + coeff * (env - rectified);
            return env;
        }
    };

    void resetDetector() {
        detector.reset();
        smoothedSensitivity.setCurrentAndTargetValue(*sensitivityParam);
        currentEnvelope.store(0.0f, std::memory_order_relaxed);
    }

    /** exp(-1 / (tau * fs)) — 0 tracks instantly, values approaching 1 track slowly. */
    float timeConstantToCoeff(float milliseconds) const {
        const float tau = std::max(milliseconds, 0.01f) * 0.001f;
        return std::exp(-1.0f / (tau * static_cast<float>(currentSampleRate)));
    }

    /** Base value plus full-range CV offset, per the "modules map raw CV to their own full
        native range" routing convention (depth is set by the cable's attenuverter). */
    static float modulated(float base, const float* cv, const juce::NormalisableRange<float>& range) {
        if (cv == nullptr)
            return base;
        return juce::jlimit(range.start, range.end, base + cv[0] * (range.end - range.start));
    }

    /** Read pointer for a CV channel, or nullptr when the channel is absent or silent.
        Mirrors the cheap first-64-sample probe the other CV-aware modules use. */
    static const float* readPointerIfActive(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) {
        if (channel >= buffer.getNumChannels())
            return nullptr;
        const float* data = buffer.getReadPointer(channel);
        const int checkLen = std::min(numSamples, 64);
        for (int i = 0; i < checkLen; ++i)
            if (data[i] != 0.0f)
                return data;
        return nullptr;
    }

    Detector detector;
    double currentSampleRate = 44100.0;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedSensitivity;
    std::atomic<float> currentEnvelope{0.0f};

    juce::AudioParameterFloat* attackParam = nullptr;
    juce::AudioParameterFloat* releaseParam = nullptr;
    juce::AudioParameterFloat* sensitivityParam = nullptr;
    juce::AudioParameterChoice* detectionParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EnvelopeFollowerModule)
};
