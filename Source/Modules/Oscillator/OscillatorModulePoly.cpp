// OscillatorModulePoly.cpp -- poly voice rendering (voices 0-7, pitch CV driven).
// The class, channel map and mono rendering: OscillatorModule.h, OscillatorModuleMono.cpp.

#include "Modules/OscillatorModule.h"

// Copies every CV this mode reads out of the buffer BEFORE the buffer is cleared: ch0-7 carry both
// the per-voice pitch CV (in) and the voice audio (out), and the shared mod CVs sit on channels the
// clear also covers. The Gate fan (ch20-27) is scanned here too: its first two channels are Audio R of
// voices 6 and 7. Safe because the module declares more outputs than its highest CV input, so
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

    for (int v = 0; v < MAX_VOICES; ++v)
        voices[v].gateEdges.scan(kGateBase + v < numChannels ? buffer.getReadPointer(kGateBase + v) : nullptr,
                                 numSamples);

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

namespace {
/** The first sample of `pitchHz[0..count)` at or above 20 Hz, or -1 when there is none. */
int firstSoundingSample(const float* pitchHz, int count) {
    for (int i = 0; i < count; ++i)
        if (pitchHz[i] >= 20.0f)
            return i;
    return -1;
}
} // namespace

void OscillatorModule::processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples) {
    const int numChannels = buffer.getNumChannels();
    const int ns = std::min(numSamples, 4096);

    cachePolyModCV(buffer, numSamples);

    // Unison/Detune/Pulse Width/Glide/Blend/Width CV (ch14-19) alias the Audio R output block -- must be read
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

    prepareUnisonMix(global.unisonCount, numSamples, global.blend, global.width);
    const bool rightWanted = unisonMix.stereo;

    const PolyVoiceSettings blockSettings{global.unisonCount, global.detuneCents, global.glideSeconds};
    const bool fixedMode = isFixedRate();
    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
        // Voice 0 falls back to the MIDI pitch when no pitch CV is patched; a voice below 20 Hz is off.
        PolyVoiceSettings settings = blockSettings;
        float basePitchHz = pitchCVCache[v][0];
        // A voice whose pitch CV starts sounding inside the block (Poly MIDI's first note on a voice) begins at that
        // sample, not at the next block: its envelope is already rising there. The CV is still gliding up, so the
        // pitch is the block's last value.
        const int start = firstSoundingSample(pitchCVCache[v].data(), ns);
        if (basePitchHz < 20.0f && start > 0) {
            settings.firstSample = start;
            basePitchHz = pitchCVCache[v][(size_t)ns - 1];
        }
        if (basePitchHz < 20.0f && v == 0)
            basePitchHz = getTargetFrequency();
        if (basePitchHz < 20.0f) {
            voices[v].active = false;
            continue;
        }
        // Fixed rate replaces the pitch only after the voice is known to be live, so voices stay note-gated.
        if (fixedMode)
            basePitchHz = baseFrequencyHz();

        // A voice going from silent to sounding is a new note and restarts its phases; voice 0 also
        // restarts on a MIDI note-on, and only that one has an earlier note to fade out of. A rising
        // Gate edge is one more new note, handled per sample in the render loops, so a voice whose
        // pitch CV never drops (Poly MIDI holds it after a note-off) still restarts on the next note.
        const bool wasActive = voices[v].active;
        if (!wasActive || (v == 0 && voice0RestartPending))
            restartVoice(voices[v], /*fadeFromOld*/ wasActive);
        voices[v].active = true;

        // A silent voice keeps its last pitch, so the next note on it slides from there.
        voices[v].glide.retarget(basePitchHz, settings.glideSeconds, currentSampleRate, glideTime(), glideCurve());
        float* outputR =
            (rightWanted && kRightBase + v < numChannels) ? buffer.getWritePointer(kRightBase + v) : nullptr;
        renderPolyVoice(v, buffer.getWritePointer(v), outputR, numSamples, basePitchHz, settings);
    }

    // Clear the shared CV channels so they do not leak downstream as audio. The Audio R block sits
    // ABOVE them at kRightBase, so this clears only the span between the two audio blocks --
    // running to numChannels would wipe the right leg we are about to write.
    for (int ch = kPolyModCVBase; ch < kRightBase && ch < numChannels; ++ch)
        buffer.clear(ch, 0, numSamples);

    // Place each rendered voice across its Audio L / Audio R pair. Voices that were skipped above
    // are still silent on both legs from the up-front clear.
    for (int v = 0; v < MAX_VOICES && v < numChannels; ++v)
        placeVoiceInStereo(buffer, v, numSamples, panCVCache.data(), ns, rightWanted);

    // Push voice 0 to visual buffer
    if (auto* vb = getVisualBuffer()) {
        vb->pushBlock(buffer.getReadPointer(0), numSamples);
    }
}

void OscillatorModule::renderPolyVoice(int voice, float* output, float* outputR, int numSamples, float basePitchHz,
                                       const PolyVoiceSettings& set) {
    const bool gliding = set.glideSeconds > 0.0f;
    const bool pitchMod = polyCV.octave || polyCV.coarse || polyCV.fine;
    if (!pitchMod && !gliding)
        renderPolyVoiceSteady(voice, output, outputR, numSamples, basePitchHz, set);
    else
        renderPolyVoiceModulated(voice, output, outputR, numSamples, basePitchHz, set, gliding);
}

// FAST PATH (no pitch CV, no glide): the phase increments are computed once per block.
void OscillatorModule::renderPolyVoiceSteady(int v, float* output, float* outputR, int numSamples, float basePitchHz,
                                             const PolyVoiceSettings& set) {
    const int ns = std::min(numSamples, 4096);
    const float freq = juce::jlimit(minRenderHz(), 20000.0f, basePitchHz);
    const float baseDt = freq / (float)currentSampleRate;
    const int wf = waveformParam->getIndex();
    const int unisonCount = unisonMix.renderCount;
    const bool hasWaveCV = polyCV.waveform;
    const bool hasLevelCV = polyCV.level;

    float ratios[MAX_UNISON];
    fillUnisonRatios(ratios, set.unisonCount, unisonCount, set.detuneCents);

    // Seed previousWaveform before the loop so the first sample doesn't spuriously trigger a
    // crossfade from the default-initialised value 0.
    if (hasWaveCV)
        voices[v].previousWaveform = wf;

    for (int s = set.firstSample; s < numSamples; ++s) {
        if (voices[v].gateEdges.firesAt(s))
            restartVoice(voices[v], /*fadeFromOld*/ true);
        const int idx = std::min(s, ns - 1);
        const int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;
        const float pulseWidth = pulseWidthRamp[(size_t)idx];

        if (hasWaveCV && wfS != voices[v].previousWaveform) {
            voices[v].fadingFromWaveform = voices[v].previousWaveform;
            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
            voices[v].previousWaveform = wfS;
        }

        const bool waveFading = hasWaveCV && voices[v].crossfadeSamplesRemaining > 0;
        const float waveAlpha =
            waveFading ? (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES : 0.0f;
        const StackContext stack{ratios, unisonCount, baseDt, wfS, voices[v].fadingFromWaveform, waveAlpha, pulseWidth};
        const StackSample stackOut = renderStack(voices[v], stack, s);

        if (waveFading)
            --voices[v].crossfadeSamplesRemaining;

        const float level = levelRamp[(size_t)idx];
        const float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
        output[s] = stackOut.left * lv;
        if (outputR != nullptr)
            outputR[s] = stackOut.right * lv;
    }
}

// PER-SAMPLE PATH (pitch CV or glide active): the phase increment is recomputed every sample.
void OscillatorModule::renderPolyVoiceModulated(int v, float* output, float* outputR, int numSamples, float basePitchHz,
                                                const PolyVoiceSettings& set, bool gliding) {
    const int ns = std::min(numSamples, 4096);
    const int wf = waveformParam->getIndex();
    const int unisonCount = unisonMix.renderCount;
    const bool hasWaveCV = polyCV.waveform;
    const bool hasLevelCV = polyCV.level;
    const float minHz = minRenderHz();

    // Each unison voice's detune ratio is a block constant, so never per sample
    float ratios[MAX_UNISON];
    fillUnisonRatios(ratios, set.unisonCount, unisonCount, set.detuneCents);

    if (hasWaveCV)
        voices[v].previousWaveform = wf;

    for (int s = set.firstSample; s < numSamples; ++s) {
        if (voices[v].gateEdges.firesAt(s))
            restartVoice(voices[v], /*fadeFromOld*/ true);
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
        freqS = juce::jlimit(minHz, 20000.0f, freqS);
        const float dtS = freqS / (float)currentSampleRate;

        const int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;
        const float pulseWidth = pulseWidthRamp[(size_t)idx];

        if (hasWaveCV && wfS != voices[v].previousWaveform) {
            voices[v].fadingFromWaveform = voices[v].previousWaveform;
            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
            voices[v].previousWaveform = wfS;
        }

        const bool waveFading = hasWaveCV && voices[v].crossfadeSamplesRemaining > 0;
        const float waveAlpha =
            waveFading ? (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES : 0.0f;
        const StackContext stack{ratios, unisonCount, dtS, wfS, voices[v].fadingFromWaveform, waveAlpha, pulseWidth};
        const StackSample stackOut = renderStack(voices[v], stack, s);

        if (waveFading)
            --voices[v].crossfadeSamplesRemaining;

        const float level = levelRamp[(size_t)idx];
        const float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
        output[s] = stackOut.left * lv;
        if (outputR != nullptr)
            outputR[s] = stackOut.right * lv;
    }
}
