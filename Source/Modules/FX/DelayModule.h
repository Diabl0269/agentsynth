#pragma once

#include "../Envelope/EnvelopeTempoSync.h"
#include "../ModuleBase.h"
#include "Modules/ParameterText.h"

class DelayModule : public ModuleBase {
public:
    static constexpr int kLevelCvChannel = 5;
    static constexpr int kPanCvChannel = 6;

    DelayModule()
        : ModuleBase("Delay", 7, 2) { // 2 audio + 5 CV (Time, Feedback, Mix, Level, Pan)
        addParameter(timeParam = new juce::AudioParameterFloat("time", "Time (ms)", 1.0f, 1000.0f, 250.0f));
        addParameter(feedbackParam = new juce::AudioParameterFloat("feedback", "Feedback", 0.0f, 0.95f, 0.5f));
        addParameter(mixParam = new juce::AudioParameterFloat("mix", "Mix", juce::NormalisableRange<float>(0.0f, 1.0f),
                                                              0.3f, synth::fractionPercentAttributes()));
        // Tempo sync: with `tempoSync` on, the delay time is a note division of the transport tempo
        // (the same division list as the LFO and ADSR) instead of the Time knob. Off (default) is the
        // plain ms delay; Time and its CV are ignored while synced. `pingPong` off (default) is the
        // plain stereo delay.
        // The text is what the card's Time/Sync switch shows for off and on.
        addParameter(tempoSyncParam = new juce::AudioParameterBool(
                         "tempoSync", "Tempo Sync", false,
                         juce::AudioParameterBoolAttributes().withStringFromValueFunction(
                             [](bool on, int) { return on ? juce::String("Sync") : juce::String("Time"); })));
        addParameter(timeDivParam =
                         new juce::AudioParameterChoice("timeDiv", "Time Div", synth::envelopeNoteDivisions(), 5));
        addParameter(pingPongParam = new juce::AudioParameterBool("pingPong", "Ping-Pong", false));
        addOutputLevelParameter(1.0f, kLevelCvChannel);
        addMuteParameter();
        addPanParameter(kPanCvChannel); // shared output stage: this module keeps its own Mix
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        this->sampleRate = sampleRate;
        int maxDelaySamples = (int)(1.0f * sampleRate); // 1 second
        delayBuffer.setSize(2, maxDelaySamples + samplesPerBlock);
        delayBuffer.clear();
        writePos = 0;
        fadeLeft_ = 0;
        lineFadeLeft_ = 0;
        lineFadeSamples_ = std::max(1, (int)(kLineFadeSeconds * sampleRate));
        fadeSamples_ = std::max(1, (int)(kCrossfadeSeconds * sampleRate));
        lineStale_ = false;

        smoothedTime.reset(sampleRate, 0.05);      // 50ms ramp for time
        smoothedFeedback.reset(sampleRate, 0.005); // 5ms ramp
        smoothedMix.reset(sampleRate, 0.005);      // 5ms ramp

        smoothedTime.setCurrentAndTargetValue(*timeParam);
        smoothedFeedback.setCurrentAndTargetValue(*feedbackParam);
        smoothedMix.setCurrentAndTargetValue(*mixParam);
        prepareOutputLevel(sampleRate);
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
            lineStale_ = true; // the line is not written while bypassed
            return;
        }
        if (isMuted()) {
            buffer.clear();
            lineStale_ = true;
            return;
        }

        juce::ignoreUnused(midiMessages);
        if (lineStale_)
            clearLine();

        // CV (ch2-4) follows the normalised convention (docs/modules/modulation.md#cv-in-normalised-units),
        // read once per block into the smoothing targets: Time's own 50 ms ramp is what keeps a
        // modulated delay time from zipping, so the CV rides that ramp like a knob move does.
        // These jacks were declared as targets long before the module had the channels — the
        // ModulationTargetBinding sweep is what caught it (channelIndex >= declared inputs).
        retargetTime(targetTimeMs(buffer));
        smoothedFeedback.setTargetValue(modulateNormalised(*feedbackParam, *feedbackParam, blockCV(buffer, 3)));
        smoothedMix.setTargetValue(modulateNormalised(*mixParam, *mixParam, blockCV(buffer, 4)));

        const int bufferSize = buffer.getNumSamples();
        if (pingPongParam->get())
            runPingPong(buffer);
        else
            runStereo(buffer);

        // Output stage, deliberately outside the feedback path above — the delay line
        // stores the pre-level signal, so lowering Level does not starve the repeats.
        applyOutputLevel(buffer, 2);

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = 2; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, bufferSize);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String cv[] = {"Time", "Feedback", "Mix", "Level", "Pan"};
        return stereoInputLabel(i, 5, cv);
    }
    juce::String getOutputPortLabel(int i) const override { return stereoOutputLabel(i); }
    int getVisibleInputPortCount() const override { return stereoVisibleInputCount(5); }
    int getVisibleOutputPortCount() const override { return stereoVisibleOutputCount(); }
    LogicalPort mapInputChannel(int raw) const override { return mapStereoPairInput(raw, 5); }
    // This module reads its audio input through mapStereoPairInput/mapStereoKeyInput
    // above -- a genuine stereo pair, eligible for render-time L->R normalling.
    bool hasStereoAudioInputPair() const override { return true; }
    LogicalPort mapOutputChannel(int raw) const override { return mapStereoPairOutput(raw); }

    // paramId binds "Time" to the "Time (ms)" knob. Level and Pan are appended last and reached through their knobs.
    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"Time", 2, "time"},
                {"Feedback", 3, "feedback"},
                {"Mix", 4, "mix"},
                {"Level", kLevelCvChannel, "outputLevel", true},
                {"Pan", kPanCvChannel, "pan", true}};
    }
    // Pure audio FX — processBlock never touches the MIDI buffer.
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }

    /** "timeDiv" gained triplets and dotted values at its end; plugin-host blobs saved before that hold a value
     *  normalised over the old eight choices. */
    int legacyChoiceCount(const juce::String& paramId) const override {
        return paramId == "timeDiv" ? synth::kLegacyNoteDivisionCount : ModuleBase::legacyChoiceCount(paramId);
    }

    ModulationCategory getModulationCategory() const override { return ModulationCategory::FX; }
    ModuleType getModuleType() const override { return ModuleType::Delay; }

private:
    /** Delay time target in ms for this block: the Time knob (plus its CV), or, with tempo sync on,
        the chosen division at the transport tempo (120 BPM with no host tempo, as the LFO and ADSR
        do). Both are held to the knob's own 1..1000 ms range -- the delay line is 1 s long. */
    float targetTimeMs(const juce::AudioBuffer<float>& buffer) const {
        if (!tempoSyncParam->get())
            return modulateNormalised(*timeParam, *timeParam, blockCV(buffer, 2));
        double bpm = 120.0;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
                if (pos->getBpm().hasValue())
                    bpm = *pos->getBpm();
        const float ms = synth::envelopeNoteDivisionSeconds(timeDivParam->getIndex(), bpm) * 1000.0f;
        return juce::jlimit(timeParam->range.start, timeParam->range.end, ms);
    }

    /** Empties the line and the repeats in it, so the module resumes from silence after a bypass or mute
        instead of playing echoes of what it heard before. */
    void clearLine() {
        delayBuffer.clear();
        fadeLeft_ = 0;
        lineFadeLeft_ = lineFadeSamples_; // what goes into the line fades in, so the first echo is not a step
        lineStale_ = false;
    }

    /** Gain on what is written into the line: 1 at rest, rising from 0 after the line was cleared. */
    float nextLineGain() {
        if (lineFadeLeft_ == 0)
            return 1.0f;
        --lineFadeLeft_;
        return 1.0f - static_cast<float>(lineFadeLeft_) / static_cast<float>(lineFadeSamples_);
    }

    /** Takes a new delay time. A small move glides (the read head speeds up or slows down a little); a jump
        of more than kJumpMs crossfades from the old read position to the new one instead, because gliding
        that far would sweep the pitch of everything in the line. A crossfade in progress finishes first;
        a jump that arrives meanwhile is taken on the block after it ends. */
    void retargetTime(float targetMs) {
        const float movedMs = std::abs(targetMs - smoothedTime.getCurrentValue());
        if (fadeLeft_ > 0) {
            if (movedMs <= kJumpMs)
                smoothedTime.setTargetValue(targetMs);
            return;
        }
        if (movedMs > kJumpMs) {
            fadeFromSamples_ = smoothedTime.getCurrentValue() * 0.001f * static_cast<float>(sampleRate);
            fadeLeft_ = fadeSamples_;
            smoothedTime.setCurrentAndTargetValue(targetMs);
            return;
        }
        smoothedTime.setTargetValue(targetMs);
    }

    /** Where this sample reads: the (smoothed) current time and, during a crossfade, the outgoing time
        with the equal-power gains that blend them. */
    struct Taps {
        float newPos = 0.0f, oldPos = 0.0f, newGain = 1.0f, oldGain = 0.0f;
        bool crossfading = false;
    };

    Taps nextTaps(int localWritePos, int delayBufferSize) {
        const float delaySamplesF = smoothedTime.getNextValue() * 0.001f * static_cast<float>(sampleRate);
        Taps t;
        t.newPos = static_cast<float>(localWritePos) - delaySamplesF + static_cast<float>(delayBufferSize);
        if (fadeLeft_ > 0) {
            const float progress = 1.0f - static_cast<float>(fadeLeft_) / static_cast<float>(fadeSamples_);
            t.crossfading = true;
            t.oldPos = static_cast<float>(localWritePos) - fadeFromSamples_ + static_cast<float>(delayBufferSize);
            t.newGain = std::sin(progress * juce::MathConstants<float>::halfPi);
            t.oldGain = std::cos(progress * juce::MathConstants<float>::halfPi);
            --fadeLeft_;
        }
        return t;
    }

    static float readTap(const float* line, int size, const Taps& t) {
        const float current = linearInterpolate(line, size, t.newPos);
        if (!t.crossfading)
            return current;
        return current * t.newGain + linearInterpolate(line, size, t.oldPos) * t.oldGain;
    }

    /** The plain stereo delay: each leg feeds back into itself. */
    void runStereo(juce::AudioBuffer<float>& buffer) {
        const int bufferSize = buffer.getNumSamples();
        const int delayBufferSize = delayBuffer.getNumSamples();
        // Only the audio pair runs through the delay line; the CV block behind it is not audio.
        const int numChannels = juce::jmin(juce::jmin(buffer.getNumChannels(), delayBuffer.getNumChannels()), 2);
        int localWritePos = writePos;

        for (int i = 0; i < bufferSize; ++i) {
            const Taps taps = nextTaps(localWritePos, delayBufferSize);
            float feedback = smoothedFeedback.getNextValue();
            float mix = smoothedMix.getNextValue();
            const float lineGain = nextLineGain();

            for (int ch = 0; ch < numChannels; ++ch) {
                auto* data = buffer.getWritePointer(ch);
                auto* delayData = delayBuffer.getWritePointer(ch);

                float input = data[i];
                float delayedSample = readTap(delayData, delayBufferSize, taps);

                delayData[localWritePos] = (input * lineGain) + (delayedSample * feedback);
                data[i] = (delayedSample * mix) + (input * (1.0f - mix));
            }

            localWritePos = (localWritePos + 1) % delayBufferSize;
        }

        writePos = localWritePos;
    }

    /** Ping-pong: the mono sum of the input enters the Left line only, and each line feeds the OTHER
        one, so the first echo lands on Left, the second on Right, the third on Left again. The dry
        signal keeps its own stereo image; only the echoes bounce. A mono buffer has no other side
        to bounce to and runs the plain delay. */
    void runPingPong(juce::AudioBuffer<float>& buffer) {
        if (buffer.getNumChannels() < 2 || delayBuffer.getNumChannels() < 2) {
            runStereo(buffer);
            return;
        }
        const int bufferSize = buffer.getNumSamples();
        const int delayBufferSize = delayBuffer.getNumSamples();
        float* left = buffer.getWritePointer(0);
        float* right = buffer.getWritePointer(1);
        float* lineL = delayBuffer.getWritePointer(0);
        float* lineR = delayBuffer.getWritePointer(1);
        int localWritePos = writePos;

        for (int i = 0; i < bufferSize; ++i) {
            const Taps taps = nextTaps(localWritePos, delayBufferSize);
            const float feedback = smoothedFeedback.getNextValue();
            const float mix = smoothedMix.getNextValue();
            const float lineGain = nextLineGain();
            const float delayedL = readTap(lineL, delayBufferSize, taps);
            const float delayedR = readTap(lineR, delayBufferSize, taps);
            const float inL = left[i];
            const float inR = right[i];

            lineL[localWritePos] = 0.5f * (inL + inR) * lineGain + delayedR * feedback;
            lineR[localWritePos] = delayedL * feedback;
            left[i] = (delayedL * mix) + (inL * (1.0f - mix));
            right[i] = (delayedR * mix) + (inR * (1.0f - mix));

            localWritePos = (localWritePos + 1) % delayBufferSize;
        }
        writePos = localWritePos;
    }

    static float linearInterpolate(const float* buffer, int bufferSize, float fractionalPos) {
        while (fractionalPos < 0.0f)
            fractionalPos += static_cast<float>(bufferSize);

        int pos0 = static_cast<int>(fractionalPos) % bufferSize;
        int pos1 = (pos0 + 1) % bufferSize;
        float frac = fractionalPos - std::floor(fractionalPos);

        return buffer[pos0] * (1.0f - frac) + buffer[pos1] * frac;
    }

    static constexpr float kJumpMs = 20.0f;            // a larger move of the time crossfades instead of gliding
    static constexpr float kCrossfadeSeconds = 0.025f; // length of that crossfade
    static constexpr float kLineFadeSeconds = 0.01f;   // fade-in of the line's input after it was cleared

    juce::AudioBuffer<float> delayBuffer;
    int writePos = 0;
    int fadeLeft_ = 0;             // samples of the running crossfade still to play (0: none)
    int fadeSamples_ = 1;          // its full length
    float fadeFromSamples_ = 0.0f; // the delay, in samples, being faded away from
    int lineFadeLeft_ = 0;         // samples of line-input fade-in still to play
    int lineFadeSamples_ = 1;
    bool lineStale_ = false; // bypassed or muted since the line was last written
    double sampleRate = 44100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedTime;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedFeedback;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMix;

    juce::AudioParameterFloat* timeParam;
    juce::AudioParameterFloat* feedbackParam;
    juce::AudioParameterFloat* mixParam;
    juce::AudioParameterBool* tempoSyncParam;
    juce::AudioParameterChoice* timeDivParam;
    juce::AudioParameterBool* pingPongParam;
};
