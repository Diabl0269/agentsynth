// OscillatorModulePoly.cpp -- poly voice rendering (voices 0-7, pitch CV driven).
// The class, channel map and mono rendering: OscillatorModule.h, OscillatorModuleMono.cpp.

#include "Modules/OscillatorModule.h"

// Copies every CV this mode reads out of the buffer BEFORE the buffer is cleared: ch0-7 carry both
// the per-voice pitch CV (in) and the voice audio (out), and the shared mod CVs sit on channels the
// clear also covers. Safe because the module declares more outputs than its highest CV input, so
// the graph hands it a private copy of any CV channel that fans out elsewhere (see the
// constructor). Do NOT reduce kNumOutputs.
void OscillatorModule::cachePolyModCV(const juce::AudioBuffer<float>& buffer, int numSamples) {
    const int numChannels = buffer.getNumChannels();
    const int ns = std::min(numSamples, 4096);

    for (int v = 0; v < MAX_VOICES; ++v) {
        if (v < numChannels)
            std::copy_n(buffer.getReadPointer(v), ns, pitchCVCache[v].data());
        else
            std::fill_n(pitchCVCache[v].data(), ns, 0.0f);
    }

    // ch8 waveform, ch9 octave, ch10 coarse, ch11 fine, ch12 level, ch13 pan.
    auto cache = [&](int channel, std::array<float, 4096>& dest) {
        const bool active = channelHasSignal(buffer, channel, numSamples);
        if (active)
            std::copy_n(buffer.getReadPointer(channel), ns, dest.data());
        else
            std::fill_n(dest.data(), ns, 0.0f);
        return active;
    };
    polyCV.waveform = cache(8, waveformCVCache);
    polyCV.octave = cache(9, octaveCVCache);
    polyCV.coarse = cache(10, coarseCVCache);
    polyCV.fine = cache(11, fineCVCache);
    polyCV.level = cache(12, levelCVCache);
    cache(modCVChannelFor(kJackPan, /*poly*/ true), panCVCache); // ch13
}

void OscillatorModule::processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples) {
    const int numChannels = buffer.getNumChannels();
    const int ns = std::min(numSamples, 4096);

    cachePolyModCV(buffer, numSamples);

    // Unison/Detune/Pulse Width/Glide CV (ch14-17) alias the Audio R output block -- must be read
    // before the clear below writes to those channels. See readGlobalParamCV.
    const GlobalParamCV global = readGlobalParamCV(buffer, numSamples);

    // Materialised once, before the voice loop -- see fillLevelRamp.
    fillLevelRamp(ns);
    fillPanRamp(ns);
    fillPulseWidthRamp(ns, global.pulseWidthPercent);

    // Clear output channels 0..getTotalNumOutputChannels()-1. Safe: every CV was cached above, and
    // the declared output count exceeds every CV input index, so JUCE gave us a private copy of
    // any CV channel that is also consumed by another node (juce_AudioProcessorGraph
    // addCopyChannelOp / isBufferNeededLater).
    for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    const PolyVoiceSettings settings{global.unisonCount, global.detuneCents, global.glideSeconds};
    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
        // Voice 0 falls back to the MIDI pitch when no pitch CV is patched; a voice below 20 Hz is off.
        float basePitchHz = pitchCVCache[v][0];
        if (basePitchHz < 20.0f && v == 0)
            basePitchHz = getTargetFrequency();
        if (basePitchHz < 20.0f)
            continue;

        // A silent voice keeps its last pitch, so the next note on it slides from there.
        voices[v].glide.retarget(basePitchHz, settings.glideSeconds, currentSampleRate);
        renderPolyVoice(v, buffer.getWritePointer(v), numSamples, basePitchHz, settings);
    }

    // Clear the shared CV channels so they do not leak downstream as audio. The Audio R block sits
    // ABOVE them at kRightBase, so this clears only the span between the two audio blocks --
    // running to numChannels would wipe the right leg we are about to write.
    for (int ch = kPolyModCVBase; ch < kRightBase && ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Place each rendered voice across its Audio L / Audio R pair. Voices that were skipped above
    // are still silent on both legs from the up-front clear.
    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v)
        placeVoiceInStereo(buffer, v, numSamples, panCVCache.data(), ns);

    // Push voice 0 to visual buffer
    if (auto* vb = getVisualBuffer()) {
        vb->pushBlock(buffer.getReadPointer(0), numSamples);
    }
}

void OscillatorModule::renderPolyVoice(int voice, float* output, int numSamples, float basePitchHz,
                                       const PolyVoiceSettings& set) {
    const bool gliding = set.glideSeconds > 0.0f;
    const bool pitchMod = polyCV.octave || polyCV.coarse || polyCV.fine;
    if (!pitchMod && !gliding)
        renderPolyVoiceSteady(voice, output, numSamples, basePitchHz, set);
    else
        renderPolyVoiceModulated(voice, output, numSamples, basePitchHz, set, gliding);
}

// FAST PATH (no pitch CV, no glide): the phase increments are computed once per block.
void OscillatorModule::renderPolyVoiceSteady(int v, float* output, int numSamples, float basePitchHz,
                                             const PolyVoiceSettings& set) {
    const int ns = std::min(numSamples, 4096);
    const float freq = juce::jlimit(20.0f, 20000.0f, basePitchHz);
    const float baseDt = freq / (float)currentSampleRate;
    const int wf = waveformParam->getIndex();
    const int unisonCount = set.unisonCount;
    const bool hasWaveCV = polyCV.waveform;
    const bool hasLevelCV = polyCV.level;

    // Pre-compute unison dt values (avoid pow in sample loop)
    float uniDts[MAX_UNISON];
    for (int u = 0; u < unisonCount; ++u) {
        const float offset = (unisonCount > 1) ? set.detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
        uniDts[u] = baseDt * std::pow(2.0f, offset / 1200.0f);
    }

    // Seed previousWaveform before the loop so the first sample doesn't spuriously trigger a
    // crossfade from the default-initialised value 0.
    if (hasWaveCV)
        voices[v].previousWaveform = wf;

    for (int s = 0; s < numSamples; ++s) {
        const int idx = std::min(s, ns - 1);
        const int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;
        const float pulseWidth = pulseWidthRamp[(size_t)idx];

        if (hasWaveCV && wfS != voices[v].previousWaveform) {
            voices[v].fadingFromWaveform = voices[v].previousWaveform;
            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
            voices[v].previousWaveform = wfS;
        }

        float sample = 0.0f;
        for (int u = 0; u < unisonCount; ++u) {
            float g;
            if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0) {
                const float alpha = (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES;
                g = generateSample(voices[v].fadingFromWaveform, voices[v].unisonOscs[u].phase, uniDts[u], pulseWidth) *
                        alpha +
                    generateSample(wfS, voices[v].unisonOscs[u].phase, uniDts[u], pulseWidth) * (1.0f - alpha);
            } else {
                g = generateSample(wfS, voices[v].unisonOscs[u].phase, uniDts[u], pulseWidth);
            }
            sample += g;
            voices[v].unisonOscs[u].phase += uniDts[u];
            if (voices[v].unisonOscs[u].phase >= 1.0f)
                voices[v].unisonOscs[u].phase -= 1.0f;
        }

        if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0)
            --voices[v].crossfadeSamplesRemaining;

        const float level = levelRamp[(size_t)idx];
        const float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
        output[s] = (sample / (float)unisonCount) * lv;
    }
}

// PER-SAMPLE PATH (pitch CV or glide active): the phase increment is recomputed every sample.
void OscillatorModule::renderPolyVoiceModulated(int v, float* output, int numSamples, float basePitchHz,
                                                const PolyVoiceSettings& set, bool gliding) {
    const int ns = std::min(numSamples, 4096);
    const int wf = waveformParam->getIndex();
    const int unisonCount = set.unisonCount;
    const bool hasWaveCV = polyCV.waveform;
    const bool hasLevelCV = polyCV.level;

    // Pre-compute each unison voice's detune ratio (a block constant, so never per sample)
    float uniMultipliers[MAX_UNISON];
    for (int u = 0; u < unisonCount; ++u) {
        const float offsetCents = (unisonCount > 1) ? set.detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
        uniMultipliers[u] = std::pow(2.0f, offsetCents / 1200.0f);
    }

    if (hasWaveCV)
        voices[v].previousWaveform = wf;

    for (int s = 0; s < numSamples; ++s) {
        const int idx = std::min(s, ns - 1);

        float extraPitchShift = 0.0f;
        if (polyCV.octave)
            extraPitchShift += std::round(octaveCVCache[idx] * 4.0f) * 12.0f;
        if (polyCV.coarse)
            extraPitchShift += std::round(coarseCVCache[idx] * 12.0f);
        if (polyCV.fine)
            extraPitchShift += fineCVCache[idx]; // cvFine * 100 / 100 == cvFine

        const float baseHz = gliding ? voices[v].glide.nextFrequency() : basePitchHz;
        float freqS = (extraPitchShift != 0.0f) ? baseHz * std::pow(2.0f, extraPitchShift / 12.0f) : baseHz;
        freqS = juce::jlimit(20.0f, 20000.0f, freqS);
        const float dtS = freqS / (float)currentSampleRate;

        const int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;
        const float pulseWidth = pulseWidthRamp[(size_t)idx];

        if (hasWaveCV && wfS != voices[v].previousWaveform) {
            voices[v].fadingFromWaveform = voices[v].previousWaveform;
            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
            voices[v].previousWaveform = wfS;
        }

        float sample = 0.0f;
        for (int u = 0; u < unisonCount; ++u) {
            const float uniDtS = dtS * uniMultipliers[u];
            float g;
            if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0) {
                const float alpha = (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES;
                g = generateSample(voices[v].fadingFromWaveform, voices[v].unisonOscs[u].phase, uniDtS, pulseWidth) *
                        alpha +
                    generateSample(wfS, voices[v].unisonOscs[u].phase, uniDtS, pulseWidth) * (1.0f - alpha);
            } else {
                g = generateSample(wfS, voices[v].unisonOscs[u].phase, uniDtS, pulseWidth);
            }
            sample += g;
            voices[v].unisonOscs[u].phase += uniDtS;
            if (voices[v].unisonOscs[u].phase >= 1.0f)
                voices[v].unisonOscs[u].phase -= 1.0f;
        }

        if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0)
            --voices[v].crossfadeSamplesRemaining;

        const float level = levelRamp[(size_t)idx];
        const float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
        output[s] = (sample / (float)unisonCount) * lv;
    }
}
