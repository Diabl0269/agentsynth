// OscillatorModuleMono.cpp -- mono voice rendering, stereo placement and the global-parameter CV read.
// The class, channel map and poly rendering: OscillatorModule.h, OscillatorModulePoly.cpp.

#include "Modules/OscillatorModule.h"

// Modulated Unison/Detune/Pulse Width/Glide, shared by both voice modes. Their CV (ch14-17) aliases
// the Audio R output block (kRightBase = 14) in both voice modes, so the caller MUST read this
// before it clears/writes those channels -- the same "read before this module overwrites its own
// input channel" rule ch0's Pitch-CV/Audio-L sharing relies on. Block-level only (first sample).
// Detune is a frequency RATIO, not a level: a step in it changes each unison oscillator's phase
// increment while the phase itself stays continuous, so it cannot click and is not smoothed. Pulse
// Width is smoothed downstream (fillPulseWidthRamp); Glide only sets the time of the next slide.
OscillatorModule::GlobalParamCV OscillatorModule::readGlobalParamCV(const juce::AudioBuffer<float>& buffer,
                                                                    int numSamples) const {
    auto first = [&](int channel) {
        return channelHasSignal(buffer, channel, numSamples) ? buffer.getReadPointer(channel)[0] : 0.0f;
    };

    GlobalParamCV result;
    result.unisonCount = juce::jlimit(
        1, 8, (int)std::round(modulateNormalised(*unisonParam, (float)unisonParam->get(), first(kUnisonCVChannel))));
    result.detuneCents = modulateNormalised(*detuneParam, detuneParam->get(), first(kDetuneCVChannel));
    result.pulseWidthPercent =
        modulateNormalised(*pulseWidthParam, pulseWidthParam->get(), first(kPulseWidthCVChannel));
    result.glideSeconds = 0.001f * modulateNormalised(*glideParam, glideParam->get(), first(kGlideCVChannel));
    return result;
}

// ---------------------------------------------------------------------------
// Mono mode processing (voice 0 only, MIDI driven)
// ---------------------------------------------------------------------------
void OscillatorModule::processMonoMode(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    juce::ignoreUnused(midiMessages); // MIDI already processed in processBlock

    if (buffer.getNumChannels() == 0)
        return;

    int numSamples = buffer.getNumSamples();

    // The mod-CV inputs (ch1-5, Pan on ch6) share raw channels with outputs this module must leave
    // silent. The render below writes only ch0 and the Audio R block (ch14+), so it reads the CV in
    // place and clears ch1-6 afterwards; an unpatched jack (the 64-sample RMS guard) reads as null,
    // which every consumer treats exactly like a block of zeros. No per-block copy or allocation.
    int numCh = buffer.getNumChannels();
    const auto activeCV = [&](int ch) -> const float* {
        return channelHasSignal(buffer, ch, numSamples) ? buffer.getReadPointer(ch) : nullptr;
    };
    const float* cvWaveformCh = activeCV(1);
    const float* cvOctaveCh = activeCV(2);
    const float* cvCoarseCh = activeCV(3);
    const float* cvFineCh = activeCV(4);
    const float* cvLevelCh = activeCV(5);
    const float* cvPanCh = activeCV(modCVChannelFor(kJackPan, /*poly*/ false));

    // Unison/Detune/Pulse Width/Glide CV (ch14-17) alias the Audio R output block -- must be read
    // before the clear below touches those channels. See readGlobalParamCV.
    const GlobalParamCV global = readGlobalParamCV(buffer, numSamples);

    // Clear every output channel except the mod-CV inputs still to be read (cleared at the end).
    // Clearing a shared input channel only ever zeroes this node's private copy: the module declares
    // 22 output channels, so JUCE's AudioProcessorGraph makes a PRIVATE COPY of any CV channel that
    // a later node also consumes (addCopyChannelOp / isBufferNeededLater).
    // WARNING: this safety depends on the output declaration in the constructor -- do NOT reduce it.
    const int lastCVChannel = modCVChannelFor(kJackPan, /*poly*/ false);
    for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
        if (ch == 0 || ch > lastCVChannel)
            buffer.clear(ch, 0, numSamples);

    float totalPitch =
        voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() + (fineParam->get() / 100.0f);
    float targetFreq = 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
    voices[0].smoothedFreq.setTargetValue(targetFreq);
    // Glide 0 leaves the pitch on the plain 5 ms frequency smoother (today's behaviour); above 0 the
    // voice slides in pitch over the set time instead and hands its pitch back to the smoother.
    voices[0].glide.retarget(targetFreq, global.glideSeconds, currentSampleRate);
    const bool gliding = global.glideSeconds > 0.0f;
    float lastBaseFreq = targetFreq;

    int baseWaveform = waveformParam->getIndex();

    const float* cvPitchCh = nullptr; // Mono mode ignores pitch CV
    auto* ch0 = buffer.getWritePointer(0);

    // modulateNormalised is a no-op when cv == 0.0f, so an unpatched jack reproduces the
    // plain behaviour exactly.
    const int unisonCount = global.unisonCount;
    const float detuneCents = global.detuneCents;

    const int levelLen = std::max(1, std::min(numSamples, 4096));
    fillLevelRamp(levelLen);
    fillPulseWidthRamp(levelLen, global.pulseWidthPercent);

    // Each unison voice's detune ratio is a block constant: computed once here, not per sample.
    float detuneMultipliers[MAX_UNISON];
    for (int u = 0; u < unisonCount; ++u) {
        const float detuneOffset = (unisonCount > 1) ? detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
        detuneMultipliers[u] = std::pow(2.0f, detuneOffset / 1200.0f);
    }

    for (int i = 0; i < numSamples; ++i) {
        float baseFreq = gliding ? voices[0].glide.nextFrequency() : voices[0].smoothedFreq.getNextValue();
        lastBaseFreq = baseFreq;

        float cvPitch = cvPitchCh ? cvPitchCh[i] : 0.0f;
        float freq = baseFreq;

        float extraPitchShift = 0.0f;

        if (cvOctaveCh) {
            int octShift = static_cast<int>(std::round(cvOctaveCh[i] * 4.0f));
            extraPitchShift += octShift * 12.0f;
        }
        if (cvCoarseCh) {
            int coarseShift = static_cast<int>(std::round(cvCoarseCh[i] * 12.0f));
            extraPitchShift += coarseShift;
        }
        if (cvFineCh) {
            float fineShift = cvFineCh[i] * 100.0f;
            extraPitchShift += fineShift / 100.0f;
        }

        if (extraPitchShift != 0.0f) {
            freq = freq * std::pow(2.0f, extraPitchShift / 12.0f);
        }

        if (cvPitch != 0.0f) {
            float clampedCV = juce::jlimit(-5.0f, 5.0f, cvPitch);
            float totalMod = clampedCV * 2.0f; // up to 2 octaves shift
            freq = freq * std::exp2(totalMod);
        }
        freq = juce::jlimit(20.0f, 20000.0f, freq);

        int waveform = baseWaveform;
        if (cvWaveformCh) {
            int waveShift = static_cast<int>(std::round(cvWaveformCh[i] * 3.0f));
            waveform = juce::jlimit(0, 3, waveform + waveShift);
        }

        float dt = static_cast<float>(freq / currentSampleRate);

        if (waveform != voices[0].previousWaveform) {
            voices[0].fadingFromWaveform = voices[0].previousWaveform;
            voices[0].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
            voices[0].previousWaveform = waveform;
        }

        float level = levelRamp[(size_t)std::min(i, levelLen - 1)];
        const float pulseWidth = pulseWidthRamp[(size_t)std::min(i, levelLen - 1)];
        if (cvLevelCh)
            level = juce::jlimit(0.0f, 1.0f, level + cvLevelCh[i]);

        // Unison generation
        float sample = 0.0f;
        for (int u = 0; u < unisonCount; ++u) {
            float uniDt = dt * detuneMultipliers[u];

            float uniSample;
            if (voices[0].crossfadeSamplesRemaining > 0) {
                float alpha = static_cast<float>(voices[0].crossfadeSamplesRemaining) / CROSSFADE_SAMPLES;
                float oldSample =
                    generateSample(voices[0].fadingFromWaveform, voices[0].unisonOscs[u].phase, uniDt, pulseWidth);
                float newSample = generateSample(waveform, voices[0].unisonOscs[u].phase, uniDt, pulseWidth);
                uniSample = oldSample * alpha + newSample * (1.0f - alpha);
            } else {
                uniSample = generateSample(waveform, voices[0].unisonOscs[u].phase, uniDt, pulseWidth);
            }

            sample += uniSample;
            voices[0].unisonOscs[u].phase += uniDt;
            if (voices[0].unisonOscs[u].phase >= 1.0f)
                voices[0].unisonOscs[u].phase -= 1.0f;
        }
        sample /= (float)unisonCount;

        if (voices[0].crossfadeSamplesRemaining > 0)
            --voices[0].crossfadeSamplesRemaining;

        ch0[i] = sample * level;
    }

    if (gliding)
        voices[0].smoothedFreq.setCurrentAndTargetValue(lastBaseFreq);

    // Place the finished mono voice across Audio L / Audio R. Done as a post-pass rather than
    // inside the render loop so the generator stays byte-identical to the plain mono path.
    fillPanRamp(numSamples);
    placeVoiceInStereo(buffer, /*voiceIndex*/ 0, numSamples, cvPanCh, numSamples);
    for (int ch = 1; ch <= lastCVChannel && ch < numCh; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Push to visual buffer
    if (auto* vb = getVisualBuffer())
        vb->pushBlock(ch0, numSamples);
}

// ---------------------------------------------------------------------------
// Stereo placement
// ---------------------------------------------------------------------------
// -------------------------------------------------------------------------
/** Splits voice `voiceIndex`, rendered in place on its Audio L channel, across Audio L and the
    matching Audio R channel at kRightBase + voiceIndex.

    `panCV` may be null; `panCVLength` is how many samples of it are valid (the poly cache is
    capped at 4096, so a longer block holds the last cached value rather than reading past it).

    The balance law (ModuleBase::panGains) leaves both legs at unity when centred, so at the
    default Pan of 0 Audio L carries exactly what it carried while this module was mono and
    Audio R is a bit-identical copy of it. */
void OscillatorModule::placeVoiceInStereo(juce::AudioBuffer<float>& buffer, int voiceIndex, int numSamples,
                                          const float* panCV, int panCVLength) {
    const int rightCh = kRightBase + voiceIndex;
    if (voiceIndex >= buffer.getNumChannels() || rightCh >= buffer.getNumChannels())
        return;

    float* left = buffer.getWritePointer(voiceIndex);
    float* right = buffer.getWritePointer(rightCh);

    // Reads the block's smoothed pan ramp (fillPanRamp — materialised once, before the voice
    // loop) so an automated Pan step glides instead of clicking (AutomationZipperTest).
    const int rampLast = std::min(std::max(0, numSamples - 1), (int)panRamp.size() - 1);
    const bool hasCV = (panCV != nullptr && panCVLength > 0);
    if (!hasCV && panRamp[0] == 0.0f && panRamp[(size_t)rampLast] == 0.0f) {
        juce::FloatVectorOperations::copy(right, left, numSamples);
        return;
    }

    for (int i = 0; i < numSamples; ++i) {
        float gainL = 1.0f;
        float gainR = 1.0f;
        panGains(panRamp[(size_t)std::min(i, rampLast)] + (hasCV ? panCV[std::min(i, panCVLength - 1)] : 0.0f), gainL,
                 gainR);
        const float sample = left[i];
        left[i] = sample * gainL;
        right[i] = sample * gainR;
    }
}
