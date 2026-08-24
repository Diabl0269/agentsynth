#pragma once

#include "../ModuleBase.h"
#include <cmath>

/**
    Dual-mode pitch / frequency shifter.

    Two independent algorithms share one module because they answer the same musical
    question ("move this signal in frequency") with opposite characters:

    - **Pitch** mode transposes by a *ratio* (semitones + cents), so harmonic
      relationships are preserved — the classic detune / octaver sound. It uses a
      two-tap crossfaded delay line: both taps read the same write head at delays that
      sweep across one window, half a window apart, summed with an equal-power window
      so the tap discontinuity always lands where that tap's gain is zero.

    - **Frequency** mode adds a *constant* offset in Hz via single-sideband modulation
      (a Hilbert transform pair driving a quadrature oscillator). Harmonics stop being
      integer multiples of the fundamental, which is what produces ring-mod-adjacent
      "alien voice" and metallic timbres.

    Feedback routes the shifted output back into the input, so each pass is shifted
    again — cascading octaves in Pitch mode, barber-pole / Shepard-tone illusions in
    Frequency mode. It is soft-clipped so the loop stays bounded at high settings.
*/
class PitchShifterModule : public ModuleBase {
public:
    PitchShifterModule()
        : ModuleBase("Pitch Shifter", 6, 2) // 2 audio + 4 CV (Pitch, Shift, Mix, Feedback)
    {
        addParameter(modeParam = new juce::AudioParameterChoice("shiftMode", "Mode", {"Pitch", "Frequency"}, 0));
        addParameter(pitchParam = new juce::AudioParameterFloat("pitch", "Pitch (semi)", -24.0f, 24.0f, 0.0f));
        addParameter(fineParam = new juce::AudioParameterFloat("fine", "Fine (cents)", -100.0f, 100.0f, 0.0f));
        addParameter(shiftParam = new juce::AudioParameterFloat("shiftHz", "Shift (Hz)", -1000.0f, 1000.0f, 0.0f));
        addParameter(windowParam =
                         new juce::AudioParameterFloat("window", "Window (ms)", kMinWindowMs, kMaxWindowMs, 50.0f));
        addParameter(feedbackParam = new juce::AudioParameterFloat("feedback", "Feedback", 0.0f, kMaxFeedback, 0.0f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", 0.0f, 1.0f, 1.0f));
        addDualIOParameter();
        addOutputLevelParameter();
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;

        // Longest tap delay equals one window, so the line only needs the largest window
        // plus a couple of samples of headroom for the interpolator.
        delayLength = (int)std::ceil(currentSampleRate * (kMaxWindowMs / 1000.0)) + 8;
        delayLine.setSize(2, delayLength);
        delayLine.clear();
        writePos = 0;

        smoothedPitch.reset(currentSampleRate, 0.02);
        smoothedShift.reset(currentSampleRate, 0.02);
        smoothedWindow.reset(currentSampleRate, 0.05);
        smoothedFeedback.reset(currentSampleRate, 0.02);
        smoothedMix.reset(currentSampleRate, 0.005);

        smoothedPitch.setCurrentAndTargetValue(*pitchParam + *fineParam * 0.01f);
        smoothedShift.setCurrentAndTargetValue(*shiftParam);
        smoothedWindow.setCurrentAndTargetValue(*windowParam);
        smoothedFeedback.setCurrentAndTargetValue(*feedbackParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        // Snapped, not ramped, so a render that starts at (or away from) unity is unchanged —
        // only a transition through unity mid-render is faded.
        unityBlend.reset(currentSampleRate, kUnityFadeSeconds);
        unityBlend.setCurrentAndTargetValue(isAtUnityRatio(smoothedPitch.getCurrentValue()) ? 1.0f : 0.0f);
        prepareOutputLevel(currentSampleRate);

        phase = 0.0f;
        oscPhase = 0.0f;
        lastShifted[0] = lastShifted[1] = 0.0f;

        for (int ch = 0; ch < 2; ++ch) {
            hilbert[ch].reset();
            hilbert[ch].setCoefficients();
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        juce::ignoreUnused(midiMessages);
        juce::ScopedNoDenormals noDenormals;

        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        if (numSamples == 0 || numChannels == 0)
            return;

        if (isMuted()) {
            buffer.clear();
            return;
        }

        if (isBypassed()) {
            // Pass dry audio through; clear CV channels so mod signals don't leak downstream
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        if (numChannels < 2 || delayLength <= 0) {
            for (int ch = 2; ch < numChannels; ++ch)
                buffer.clear(ch, 0, numSamples);
            return;
        }

        const float* cvPitch = (numChannels > 2) ? buffer.getReadPointer(2) : nullptr;
        const float* cvShift = (numChannels > 3) ? buffer.getReadPointer(3) : nullptr;
        const float* cvMix = (numChannels > 4) ? buffer.getReadPointer(4) : nullptr;
        const float* cvFeedback = (numChannels > 5) ? buffer.getReadPointer(5) : nullptr;

        const bool cvPitchActive = isChannelActive(cvPitch, numSamples);
        const bool cvShiftActive = isChannelActive(cvShift, numSamples);
        const bool cvMixActive = isChannelActive(cvMix, numSamples);
        const bool cvFeedbackActive = isChannelActive(cvFeedback, numSamples);

        smoothedPitch.setTargetValue(*pitchParam + *fineParam * 0.01f);
        smoothedShift.setTargetValue(*shiftParam);
        smoothedWindow.setTargetValue(*windowParam);
        smoothedFeedback.setTargetValue(*feedbackParam);
        smoothedMix.setTargetValue(*mixParam);

        const bool frequencyMode = (modeParam->getIndex() == 1);
        const float twoPi = juce::MathConstants<float>::twoPi;

        float* outL = buffer.getWritePointer(0);
        float* outR = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i) {
            // Raw CV is normalised [-1, 1]; each target maps it onto its full native range
            // (depth is set by the attenuverter on the cable, never by an internal knob).
            const float pitchMod = cvPitchActive ? cvPitch[i] * 24.0f : 0.0f;
            const float shiftMod = cvShiftActive ? cvShift[i] * 1000.0f : 0.0f;
            const float mixMod = cvMixActive ? cvMix[i] : 0.0f;
            const float feedbackMod = cvFeedbackActive ? cvFeedback[i] * kMaxFeedback : 0.0f;

            const float semitones = juce::jlimit(-48.0f, 48.0f, smoothedPitch.getNextValue() + pitchMod);
            const float shiftHz = juce::jlimit(-2000.0f, 2000.0f, smoothedShift.getNextValue() + shiftMod);
            const float windowMs = smoothedWindow.getNextValue();
            const float feedback = juce::jlimit(0.0f, kMaxFeedback, smoothedFeedback.getNextValue() + feedbackMod);
            const float mix = juce::jlimit(0.0f, 1.0f, smoothedMix.getNextValue() + mixMod);

            const float dryL = outL[i];
            const float dryR = outR[i];

            // Soft-clip the feedback so a long shifted-and-refed chain cannot run away.
            const float inL = dryL + (feedback > 0.0f ? std::tanh(feedback * lastShifted[0]) : 0.0f);
            const float inR = dryR + (feedback > 0.0f ? std::tanh(feedback * lastShifted[1]) : 0.0f);

            // Always keep the delay line warm so switching modes mid-signal doesn't
            // replay a stale buffer.
            delayLine.setSample(0, writePos, inL);
            delayLine.setSample(1, writePos, inR);

            float wetL, wetR;

            if (frequencyMode) {
                oscPhase += twoPi * shiftHz / (float)currentSampleRate;
                while (oscPhase >= twoPi)
                    oscPhase -= twoPi;
                while (oscPhase < 0.0f)
                    oscPhase += twoPi;

                const float cosT = std::cos(oscPhase);
                const float sinT = std::sin(oscPhase);

                wetL = hilbert[0].shift(inL, cosT, sinT);
                wetR = hilbert[1].shift(inR, cosT, sinT);
            } else {
                const float ratio = std::pow(2.0f, semitones / 12.0f);
                const float windowSamples = juce::jmax(32.0f, windowMs * 0.001f * (float)currentSampleRate);

                // At unity the two taps would sit at fixed, different delays and comb-filter the
                // input, so Pitch = 0 emits the signal untransposed instead. Entering and leaving
                // that state swaps between two genuinely different signals, so it is CROSSFADED
                // over kUnityFadeSeconds rather than switched: a slow Pitch/Fine sweep parks
                // inside the epsilon window for tens of samples, and switching there was an
                // audible click (caught by AutomationZipperTests). Time-based rather than
                // ratio-based, so the fade cannot be outrun by a fast sweep.
                unityBlend.setTargetValue(std::abs(ratio - 1.0f) < kUnityRatioEpsilon ? 1.0f : 0.0f);
                const float dryWeight = unityBlend.getNextValue();

                float tapL = inL;
                float tapR = inR;

                if (dryWeight < 1.0f) {
                    // r(t) = w(t) - d(t) must advance at `ratio`, so d' = 1 - ratio; normalising
                    // by the window turns that into a phase that sweeps one window per cycle.
                    phase += (1.0f - ratio) / windowSamples;
                    while (phase >= 1.0f)
                        phase -= 1.0f;
                    while (phase < 0.0f)
                        phase += 1.0f;

                    const float fracA = phase;
                    const float fracB = (fracA < 0.5f) ? fracA + 0.5f : fracA - 0.5f;

                    // Equal-power window: gainA^2 + gainB^2 == 1, and each gain is zero exactly
                    // where its own tap wraps, so the wrap is inaudible.
                    const float gainA = std::sin(juce::MathConstants<float>::pi * fracA);
                    const float gainB = std::abs(std::cos(juce::MathConstants<float>::pi * fracA));

                    const float delayA = juce::jlimit(kMinReadDelay, windowSamples, fracA * windowSamples);
                    const float delayB = juce::jlimit(kMinReadDelay, windowSamples, fracB * windowSamples);

                    tapL = gainA * readDelayed(0, delayA) + gainB * readDelayed(0, delayB);
                    tapR = gainA * readDelayed(1, delayA) + gainB * readDelayed(1, delayB);
                } else {
                    phase = 0.0f;
                }

                wetL = dryWeight * inL + (1.0f - dryWeight) * tapL;
                wetR = dryWeight * inR + (1.0f - dryWeight) * tapR;
            }

            lastShifted[0] = wetL;
            lastShifted[1] = wetR;

            outL[i] = dryL * (1.0f - mix) + wetL * mix;
            outR[i] = dryR * (1.0f - mix) + wetR * mix;

            if (++writePos >= delayLength)
                writePos = 0;
        }

        // Output stage, outside the feedback write above — lowering Level does not starve the
        // shifted tail. Applied before the scope push so the visualiser shows the real output.
        applyOutputLevel(buffer, 2);

        if (auto* vb = getVisualBuffer()) {
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(outL[i]);
        }

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Pitch", "Shift", "Mix", "Feedback"};
        return stereoInputLabel(i, 4, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(4); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 4); }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Pitch", 2}, {"Shift", 3}, {"Mix", 4}, {"Feedback", 5}};
    }

    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::PitchShifter; }

    double getTailLengthSeconds() const override { return kMaxWindowMs / 1000.0; }

private:
    static constexpr float kMinWindowMs = 10.0f;
    static constexpr float kMaxWindowMs = 100.0f;
    static constexpr float kMaxFeedback = 0.95f;
    // ~0.0017 semitones — narrow enough that sweeping the knob passes straight through it.
    static constexpr float kUnityRatioEpsilon = 1.0e-4f;
    // Crossfade time in and out of the unity pass-through. Short enough to feel instant, long
    // enough that the swap between the tap output and the dry signal cannot click.
    static constexpr double kUnityFadeSeconds = 0.005;

    static bool isAtUnityRatio(float semitones) noexcept {
        return std::abs(std::pow(2.0f, semitones / 12.0f) - 1.0f) < kUnityRatioEpsilon;
    }
    // The cubic interpolator looks one sample ahead of the read index, so never read closer
    // than two samples behind the write head (both taps are windowed to ~0 gain there anyway).
    static constexpr float kMinReadDelay = 2.0f;

    static bool isChannelActive(const float* channel, int numSamples) {
        if (!channel)
            return false;
        for (int i = 0; i < numSamples; ++i) {
            if (channel[i] != 0.0f)
                return true;
        }
        return false;
    }

    /** Reads the delay line `delaySamples` behind the write head with cubic Hermite
        interpolation — linear interpolation would audibly dull a transposed signal. */
    float readDelayed(int ch, float delaySamples) const {
        const float pos = (float)writePos - delaySamples;
        int index = (int)std::floor(pos);
        const float t = pos - (float)index;

        const float* data = delayLine.getReadPointer(ch);
        const auto wrap = [this](int i) { return ((i % delayLength) + delayLength) % delayLength; };

        const float x0 = data[wrap(index - 1)];
        const float x1 = data[wrap(index)];
        const float x2 = data[wrap(index + 1)];
        const float x3 = data[wrap(index + 2)];

        const float c0 = x1;
        const float c1 = 0.5f * (x2 - x0);
        const float c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3;
        const float c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);

        return ((c3 * t + c2) * t + c1) * t + c0;
    }

    /** Hilbert transform pair (two cascaded 2nd-order allpass chains whose outputs stay
        ~90 degrees apart across the audio band) feeding a single-sideband modulator. */
    struct HilbertShifter {
        struct Allpass {
            float aSquared = 0.0f;
            float x1 = 0.0f, x2 = 0.0f, y1 = 0.0f, y2 = 0.0f;

            // H(z) = (a^2 - z^-2) / (1 - a^2 z^-2)
            float process(float x) {
                const float y = aSquared * (x + y2) - x2;
                x2 = x1;
                x1 = x;
                y2 = y1;
                y1 = y;
                return y;
            }

            void reset() { x1 = x2 = y1 = y2 = 0.0f; }
        };

        // Olli Niemitalo's allpass coefficient pair — the standard wideband 90-degree
        // approximation used for SSB modulation.
        void setCoefficients() {
            static constexpr float kInPhase[4] = {0.6923878f, 0.9360654322959f, 0.9882295226860f, 0.9987488452737f};
            static constexpr float kQuadrature[4] = {0.4021921162426f, 0.8561710882420f, 0.9722909545651f,
                                                     0.9952884791278f};
            for (int i = 0; i < 4; ++i) {
                inPhase[i].aSquared = kInPhase[i] * kInPhase[i];
                quadrature[i].aSquared = kQuadrature[i] * kQuadrature[i];
            }
        }

        void reset() {
            for (int i = 0; i < 4; ++i) {
                inPhase[i].reset();
                quadrature[i].reset();
            }
            delayed = 0.0f;
        }

        /** Returns the input shifted up by the oscillator whose current cos/sin are given.
            A negative oscillator frequency shifts down; no separate branch is needed. */
        float shift(float x, float cosT, float sinT) {
            float i = x;
            for (auto& stage : inPhase)
                i = stage.process(i);

            float q = x;
            for (auto& stage : quadrature)
                q = stage.process(q);

            // The in-phase chain is defined relative to a one-sample-delayed signal.
            const float iDelayed = delayed;
            delayed = i;

            // The quadrature branch leads the in-phase branch by 90 degrees, so the upper
            // sideband is the *sum* here; measured rejection of the lower one is ~55 dB.
            return iDelayed * cosT + q * sinT;
        }

        Allpass inPhase[4];
        Allpass quadrature[4];
        float delayed = 0.0f;
    };

    double currentSampleRate = 44100.0;

    juce::AudioBuffer<float> delayLine;
    int delayLength = 0;
    int writePos = 0;

    float phase = 0.0f;    // pitch-mode crossfade position, [0, 1)
    float oscPhase = 0.0f; // frequency-mode quadrature oscillator, [0, 2pi)
    float lastShifted[2] = {0.0f, 0.0f};

    HilbertShifter hilbert[2];

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedPitch;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedShift;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedWindow;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> unityBlend;

    juce::AudioParameterChoice* modeParam = nullptr;
    juce::AudioParameterFloat* pitchParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* shiftParam = nullptr;
    juce::AudioParameterFloat* windowParam = nullptr;
    juce::AudioParameterFloat* feedbackParam = nullptr;
    juce::AudioParameterFloat* mixParam = nullptr;
};
