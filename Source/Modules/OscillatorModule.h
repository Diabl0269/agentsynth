#pragma once

#include "ModuleBase.h"
#include <cmath>

class OscillatorModule : public ModuleBase {
public:
    OscillatorModule()
        : ModuleBase("Oscillator", 14,
                     14) // 14 in: 8 per-voice pitch CV (0-7) + 6 shared mod CV (8-13).
                         // 14 out declared so JUCE copies shared-mod-CV inputs when they fan out to
                         // multiple downstream nodes (avoids buffer aliasing when inputChan >= numOuts).
                         // Only channels 0-7 carry audio; 8-13 are silent pass-through outputs.
                         // 14 outputs (not 8): channels 8-13 are silent pass-through outputs; this also
                         // makes JUCE copy shared CV input buffers so our post-render clear can't
                         // corrupt them — see processPolyMode clear note.
    {
        addParameter(waveformParam = new juce::AudioParameterChoice("waveform", "Waveform",
                                                                    {"Sine", "Square", "Saw", "Triangle"}, 0));
        addParameter(octaveParam = new juce::AudioParameterInt(juce::ParameterID("octave", 1), "Octave", -4, 4, 0));
        addParameter(coarseParam = new juce::AudioParameterInt(juce::ParameterID("coarse", 1), "Coarse", -12, 12, 0));
        addParameter(fineParam = new juce::AudioParameterFloat("fine", "Fine", -100.0f, 100.0f, 0.0f));
        addParameter(levelParam = new juce::AudioParameterFloat("level", "Level", 0.0f, 1.0f, 1.0f));
        addParameter(polyParam = new juce::AudioParameterBool("poly", "Poly", false));
        addParameter(unisonParam = new juce::AudioParameterInt(juce::ParameterID("unison", 1), "Unison", 1, 8, 1));
        addParameter(detuneParam = new juce::AudioParameterFloat("detune", "Detune", 0.0f, 100.0f, 0.0f));
        addMuteParameter();
        enableVisualBuffer(true);
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        juce::ignoreUnused(samplesPerBlock);
        currentSampleRate = sampleRate;

        float initPitch = voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() +
                          (fineParam->get() / 100.0f);
        float initFreq = 440.0f * std::pow(2.0f, (initPitch - 69.0f) / 12.0f);

        for (int v = 0; v < MAX_VOICES; ++v) {
            voices[v].smoothedFreq.reset(sampleRate, 0.005);
            voices[v].smoothedFreq.setCurrentAndTargetValue(initFreq);
        }
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
        if (isBypassed() || isMuted()) {
            buffer.clear();
            return;
        }

        // Always process MIDI for voice 0 (fallback when no pitch CV connected)
        for (const auto metadata : midiMessages) {
            auto msg = metadata.getMessage();
            if (msg.isNoteOn())
                voices[0].lastMidiNote = (float)msg.getNoteNumber();
        }

        if (polyParam->get()) {
            processPolyMode(buffer, buffer.getNumSamples());
        } else {
            // Clear all channels except 0 at the start to prevent reading garbage
            // but we need to save CV data first if we want to use it.
            // Wait! In mono mode, channels 1-5 are CV inputs.
            processMonoMode(buffer, midiMessages);
        }
    }

    float getTargetFrequency() const {
        float totalPitch = voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() +
                           (fineParam->get() / 100.0f);
        return 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
    }

    std::vector<ModulationTarget> getModulationTargets() const override {
        if (polyParam->get())
            return {{"Waveform", 8}, {"Octave", 9}, {"Coarse", 10}, {"Fine", 11}, {"Level", 12}};
        return {{"Pitch", 0}, {"Waveform", 1}, {"Octave", 2}, {"Coarse", 3}, {"Fine", 4}, {"Level", 5}};
    }
    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Pitch", "Waveform", "Octave", "Coarse", "Fine", "Level"};
        return (i >= 0 && i < 6) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int) const override { return "Audio"; }
    int getVisibleInputPortCount() const override { return 6; }
    int getVisibleOutputPortCount() const override { return 1; }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Oscillator; }
    ModuleType getModuleType() const override { return ModuleType::Oscillator; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (polyParam->get()) {
            // Poly mode: raw 0-7 = per-voice Pitch fan; raw 8-12 = shared ModCV
            if (raw >= 0 && raw <= 7) {
                p.visibleJackIndex = 0;
                p.role = PortRole::Pitch;
                p.isPolyGroupHead = (raw == 0);
                p.polyVoiceSpan = (raw == 0) ? 8 : 1;
                return p;
            }
            if (raw == 8) {
                p.visibleJackIndex = 1;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 9) {
                p.visibleJackIndex = 2;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 10) {
                p.visibleJackIndex = 3;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 11) {
                p.visibleJackIndex = 4;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
            if (raw == 12) {
                p.visibleJackIndex = 5;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        } else {
            // Mono mode: raw 0-5 = ModCV jacks 0-5
            if (raw >= 0 && raw <= 5) {
                p.visibleJackIndex = raw;
                p.role = PortRole::ModCV;
                p.isPolyGroupHead = true;
                p.polyVoiceSpan = 1;
                return p;
            }
        }
        return ModuleBase::mapInputChannel(raw);
    }

    bool isAutoPromotableModTarget(int dstChannel) const override {
        if (polyParam->get())
            return false;
        return ModuleBase::isAutoPromotableModTarget(dstChannel);
    }

private:
    // -------------------------------------------------------------------------
    // Constants
    // -------------------------------------------------------------------------
    static constexpr int MAX_VOICES = 8;
    static constexpr int MAX_UNISON = 8;
    static constexpr int CROSSFADE_SAMPLES = 64;

    // -------------------------------------------------------------------------
    // Per-voice state
    // -------------------------------------------------------------------------
    struct UnisonOsc {
        float phase = 0.0f;
    };

    struct VoiceState {
        UnisonOsc unisonOscs[MAX_UNISON];
        juce::SmoothedValue<float> smoothedFreq;
        float lastMidiNote = 69.0f;
        int previousWaveform = 0;
        int fadingFromWaveform = 0;
        int crossfadeSamplesRemaining = 0;
    };

    VoiceState voices[MAX_VOICES];

    // -------------------------------------------------------------------------
    // Mono mode processing (voice 0 only, MIDI driven)
    // -------------------------------------------------------------------------
    void processMonoMode(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
        juce::ignoreUnused(midiMessages); // MIDI already processed in processBlock

        if (buffer.getNumChannels() == 0)
            return;

        int numSamples = buffer.getNumSamples();

        // Save CV channel data before clearing the buffer.
        // Channel 0 is shared between CV pitch input and audio output;
        // in mono mode we ignore pitch CV (it may contain Hz from PolyMidi).
        int numCh = buffer.getNumChannels();
        juce::HeapBlock<float> cvWaveformSaved(numSamples);
        juce::HeapBlock<float> cvOctaveSaved(numSamples);
        juce::HeapBlock<float> cvCoarseSaved(numSamples);
        juce::HeapBlock<float> cvFineSaved(numSamples);
        juce::HeapBlock<float> cvLevelSaved(numSamples);

        // Zero them initially
        juce::FloatVectorOperations::clear(cvWaveformSaved, numSamples);
        juce::FloatVectorOperations::clear(cvOctaveSaved, numSamples);
        juce::FloatVectorOperations::clear(cvCoarseSaved, numSamples);
        juce::FloatVectorOperations::clear(cvFineSaved, numSamples);
        juce::FloatVectorOperations::clear(cvLevelSaved, numSamples);

        // Helper to check if a channel is active
        auto isChannelActive = [&](int ch) {
            if (ch >= numCh)
                return false;
            auto* data = buffer.getReadPointer(ch);
            float rms = 0.0f;
            for (int i = 0; i < std::min(numSamples, 64); ++i)
                rms += data[i] * data[i];
            return (rms / std::min(numSamples, 64)) > 1e-6f;
        };

        if (isChannelActive(1))
            juce::FloatVectorOperations::copy(cvWaveformSaved, buffer.getReadPointer(1), numSamples);
        if (isChannelActive(2))
            juce::FloatVectorOperations::copy(cvOctaveSaved, buffer.getReadPointer(2), numSamples);
        if (isChannelActive(3))
            juce::FloatVectorOperations::copy(cvCoarseSaved, buffer.getReadPointer(3), numSamples);
        if (isChannelActive(4))
            juce::FloatVectorOperations::copy(cvFineSaved, buffer.getReadPointer(4), numSamples);
        if (isChannelActive(5))
            juce::FloatVectorOperations::copy(cvLevelSaved, buffer.getReadPointer(5), numSamples);

        // Clear output channels 0..getTotalNumOutputChannels()-1 (==14). This range includes the
        // shared mod-CV input channels (1-5 mono), but clearing them here is SAFE because:
        // (a) those CVs were already cached above (into cv*Saved) before this clear; and
        // (b) the module declares 14 output channels, so JUCE's AudioProcessorGraph treats
        //     inputChan < numOutputs and makes a PRIVATE COPY of any CV channel that is also
        //     consumed later by another downstream node (juce_AudioProcessorGraph addCopyChannelOp
        //     / isBufferNeededLater). So this only zeroes our private copy, never the shared
        //     source buffer.
        // WARNING: this safety depends on the 14-output declaration in the constructor —
        //          do NOT reduce it back to 8.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numCh; ++ch)
            buffer.clear(ch, 0, numSamples);

        float totalPitch = voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() +
                           (fineParam->get() / 100.0f);
        float targetFreq = 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
        voices[0].smoothedFreq.setTargetValue(targetFreq);

        int baseWaveform = waveformParam->getIndex();

        const float* cvPitchCh = nullptr; // Mono mode ignores pitch CV
        const float* cvWaveformCh = (numCh > 1) ? cvWaveformSaved.get() : nullptr;
        const float* cvOctaveCh = (numCh > 2) ? cvOctaveSaved.get() : nullptr;
        const float* cvCoarseCh = (numCh > 3) ? cvCoarseSaved.get() : nullptr;
        const float* cvFineCh = (numCh > 4) ? cvFineSaved.get() : nullptr;
        const float* cvLevelCh = (numCh > 5) ? cvLevelSaved.get() : nullptr;
        auto* ch0 = buffer.getWritePointer(0);

        int unisonCount = unisonParam->get();
        float detuneCents = detuneParam->get();

        for (int i = 0; i < numSamples; ++i) {
            float baseFreq = voices[0].smoothedFreq.getNextValue();

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

            float level = levelParam->get();
            if (cvLevelCh)
                level = juce::jlimit(0.0f, 1.0f, level + cvLevelCh[i]);

            // Unison generation
            float sample = 0.0f;
            for (int u = 0; u < unisonCount; ++u) {
                float detuneOffset = (unisonCount > 1) ? detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
                float detuneMultiplier = std::pow(2.0f, detuneOffset / 1200.0f);
                float uniDt = dt * detuneMultiplier;

                float uniSample;
                if (voices[0].crossfadeSamplesRemaining > 0) {
                    float alpha = static_cast<float>(voices[0].crossfadeSamplesRemaining) / CROSSFADE_SAMPLES;
                    float oldSample =
                        generateSample(voices[0].fadingFromWaveform, voices[0].unisonOscs[u].phase, uniDt);
                    float newSample = generateSample(waveform, voices[0].unisonOscs[u].phase, uniDt);
                    uniSample = oldSample * alpha + newSample * (1.0f - alpha);
                } else {
                    uniSample = generateSample(waveform, voices[0].unisonOscs[u].phase, uniDt);
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

        // Push to visual buffer
        if (auto* vb = getVisualBuffer()) {
            for (int i = 0; i < numSamples; ++i) {
                vb->pushSample(ch0[i]);
            }
        }
    }

    // -------------------------------------------------------------------------
    // Poly mode processing (voices 0-7, pitch CV driven)
    // -------------------------------------------------------------------------
    void processPolyMode(juce::AudioBuffer<float>& buffer, int numSamples) {
        int numChannels = buffer.getNumChannels();
        int ns = std::min(numSamples, 4096);

        // Helper to check if a channel is active (same RMS guard as mono mode)
        auto isChannelActive = [&](int ch) {
            if (ch >= numChannels)
                return false;
            auto* data = buffer.getReadPointer(ch);
            float rms = 0.0f;
            int checkLen = std::min(numSamples, 64);
            for (int i = 0; i < checkLen; ++i)
                rms += data[i] * data[i];
            return (rms / (float)checkLen) > 1e-6f;
        };

        // Save pitch CVs (channels 0-7) before clearing buffer
        for (int v = 0; v < MAX_VOICES; ++v) {
            if (v < numChannels)
                std::copy_n(buffer.getReadPointer(v), ns, pitchCVCache[v].data());
            else
                std::fill_n(pitchCVCache[v].data(), ns, 0.0f);
        }

        // Cache shared mod CVs (channels 8-12) before clearing buffer
        // ch8 -> waveformCVCache, ch9 -> octaveCVCache, ch10 -> coarseCVCache
        // ch11 -> fineCVCache, ch12 -> levelCVCache
        bool hasWaveCV = isChannelActive(8);
        bool hasOctCV = isChannelActive(9);
        bool hasCoarseCV = isChannelActive(10);
        bool hasFineCV = isChannelActive(11);
        bool hasLevelCV = isChannelActive(12);

        if (hasWaveCV)
            std::copy_n(buffer.getReadPointer(8), ns, waveformCVCache.data());
        else
            std::fill_n(waveformCVCache.data(), ns, 0.0f);

        if (hasOctCV)
            std::copy_n(buffer.getReadPointer(9), ns, octaveCVCache.data());
        else
            std::fill_n(octaveCVCache.data(), ns, 0.0f);

        if (hasCoarseCV)
            std::copy_n(buffer.getReadPointer(10), ns, coarseCVCache.data());
        else
            std::fill_n(coarseCVCache.data(), ns, 0.0f);

        if (hasFineCV)
            std::copy_n(buffer.getReadPointer(11), ns, fineCVCache.data());
        else
            std::fill_n(fineCVCache.data(), ns, 0.0f);

        if (hasLevelCV)
            std::copy_n(buffer.getReadPointer(12), ns, levelCVCache.data());
        else
            std::fill_n(levelCVCache.data(), ns, 0.0f);

        bool pitchMod = hasOctCV || hasCoarseCV || hasFineCV;

        // Clear output channels 0..getTotalNumOutputChannels()-1 (==14). This range includes the
        // shared mod-CV input channels (8-12 poly), but clearing them here is SAFE because:
        // (a) those CVs were already cached above (into *CVCache) before this clear; and
        // (b) the module declares 14 output channels, so JUCE's AudioProcessorGraph treats
        //     inputChan < numOutputs and makes a PRIVATE COPY of any CV channel that is also
        //     consumed later by another downstream node (juce_AudioProcessorGraph addCopyChannelOp
        //     / isBufferNeededLater). So this only zeroes our private copy, never the shared
        //     source buffer.
        // WARNING: this safety depends on the 14-output declaration in the constructor —
        //          do NOT reduce it back to 8.
        for (int ch = 0; ch < getTotalNumOutputChannels() && ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        for (int v = 0; v < MAX_VOICES && v < numChannels; ++v) {
            float* output = buffer.getWritePointer(v);
            const float* pitchCVs = pitchCVCache[v].data();

            // Check first sample to skip inactive voices
            float firstPitch = pitchCVs[0];
            if (firstPitch < 20.0f && v == 0) {
                // Voice 0 MIDI fallback
                float totalPitch = voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() +
                                   (fineParam->get() / 100.0f);
                firstPitch = 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
            }
            if (firstPitch < 20.0f)
                continue; // Skip entirely

            // Get base frequency for this voice (use first sample)
            float basePitchHz = pitchCVs[0];
            if (basePitchHz < 20.0f && v == 0) {
                float totalPitch = voices[0].lastMidiNote + (octaveParam->get() * 12.0f) + (float)coarseParam->get() +
                                   (fineParam->get() / 100.0f);
                basePitchHz = 440.0f * std::pow(2.0f, (totalPitch - 69.0f) / 12.0f);
            }
            if (basePitchHz < 20.0f)
                continue;

            float freq = juce::jlimit(20.0f, 20000.0f, basePitchHz);
            float baseDt = freq / (float)currentSampleRate;
            int wf = waveformParam->getIndex();
            float level = levelParam->get();
            int unisonCount = unisonParam->get();
            float detuneCents = detuneParam->get();

            if (!pitchMod) {
                // ---- FAST PATH (no pitch CV): precomputed uniDts, byte-identical to original ----
                // Pre-compute unison dt values (avoid pow in sample loop)
                float uniDts[MAX_UNISON];
                for (int u = 0; u < unisonCount; ++u) {
                    float offset = (unisonCount > 1) ? detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
                    uniDts[u] = baseDt * std::pow(2.0f, offset / 1200.0f);
                }

                // Seed previousWaveform before the loop so the first sample doesn't
                // spuriously trigger a crossfade from the default-initialised value 0.
                if (hasWaveCV)
                    voices[v].previousWaveform = wf;

                for (int s = 0; s < numSamples; ++s) {
                    int idx = std::min(s, ns - 1);
                    int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;

                    if (hasWaveCV) {
                        if (wfS != voices[v].previousWaveform) {
                            voices[v].fadingFromWaveform = voices[v].previousWaveform;
                            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
                            voices[v].previousWaveform = wfS;
                        }
                    }

                    float sample = 0.0f;
                    for (int u = 0; u < unisonCount; ++u) {
                        float g;
                        if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0) {
                            float alpha = (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES;
                            g = generateSample(voices[v].fadingFromWaveform, voices[v].unisonOscs[u].phase, uniDts[u]) *
                                    alpha +
                                generateSample(wfS, voices[v].unisonOscs[u].phase, uniDts[u]) * (1.0f - alpha);
                        } else {
                            g = generateSample(wfS, voices[v].unisonOscs[u].phase, uniDts[u]);
                        }
                        sample += g;
                        voices[v].unisonOscs[u].phase += uniDts[u];
                        if (voices[v].unisonOscs[u].phase >= 1.0f)
                            voices[v].unisonOscs[u].phase -= 1.0f;
                    }

                    if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0)
                        --voices[v].crossfadeSamplesRemaining;

                    float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
                    output[s] = (sample / (float)unisonCount) * lv;
                }
            } else {
                // ---- PER-SAMPLE PATH (pitch CV active): recompute dt each sample ----
                // Pre-compute unison offset cents (avoid recomputing per-sample)
                float uniOffsetCents[MAX_UNISON];
                for (int u = 0; u < unisonCount; ++u) {
                    uniOffsetCents[u] = (unisonCount > 1) ? detuneCents * (2.0f * u / (unisonCount - 1) - 1.0f) : 0.0f;
                }

                // Seed previousWaveform before the loop so the first sample doesn't
                // spuriously trigger a crossfade from the default-initialised value 0.
                if (hasWaveCV)
                    voices[v].previousWaveform = wf;

                for (int s = 0; s < numSamples; ++s) {
                    int idx = std::min(s, ns - 1);

                    float extraPitchShift = 0.0f;
                    if (hasOctCV)
                        extraPitchShift += std::round(octaveCVCache[idx] * 4.0f) * 12.0f;
                    if (hasCoarseCV)
                        extraPitchShift += std::round(coarseCVCache[idx] * 12.0f);
                    if (hasFineCV)
                        extraPitchShift += fineCVCache[idx]; // cvFine * 100 / 100 == cvFine

                    float freqS =
                        (extraPitchShift != 0.0f) ? basePitchHz * std::pow(2.0f, extraPitchShift / 12.0f) : basePitchHz;
                    freqS = juce::jlimit(20.0f, 20000.0f, freqS);
                    float dtS = freqS / (float)currentSampleRate;

                    int wfS = hasWaveCV ? juce::jlimit(0, 3, wf + (int)std::round(waveformCVCache[idx] * 3.0f)) : wf;

                    if (hasWaveCV) {
                        if (wfS != voices[v].previousWaveform) {
                            voices[v].fadingFromWaveform = voices[v].previousWaveform;
                            voices[v].crossfadeSamplesRemaining = CROSSFADE_SAMPLES;
                            voices[v].previousWaveform = wfS;
                        }
                    }

                    float sample = 0.0f;
                    for (int u = 0; u < unisonCount; ++u) {
                        float uniDtS = dtS * std::pow(2.0f, uniOffsetCents[u] / 1200.0f);
                        float g;
                        if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0) {
                            float alpha = (float)voices[v].crossfadeSamplesRemaining / (float)CROSSFADE_SAMPLES;
                            g = generateSample(voices[v].fadingFromWaveform, voices[v].unisonOscs[u].phase, uniDtS) *
                                    alpha +
                                generateSample(wfS, voices[v].unisonOscs[u].phase, uniDtS) * (1.0f - alpha);
                        } else {
                            g = generateSample(wfS, voices[v].unisonOscs[u].phase, uniDtS);
                        }
                        sample += g;
                        voices[v].unisonOscs[u].phase += uniDtS;
                        if (voices[v].unisonOscs[u].phase >= 1.0f)
                            voices[v].unisonOscs[u].phase -= 1.0f;
                    }

                    if (hasWaveCV && voices[v].crossfadeSamplesRemaining > 0)
                        --voices[v].crossfadeSamplesRemaining;

                    float lv = hasLevelCV ? juce::jlimit(0.0f, 1.0f, level + levelCVCache[idx]) : level;
                    output[s] = (sample / (float)unisonCount) * lv;
                }
            }
        }

        // Clear CV channels (>= 8) so they do not leak downstream
        for (int ch = 8; ch < numChannels; ++ch)
            buffer.clear(ch, 0, numSamples);

        // Push voice 0 to visual buffer
        if (auto* vb = getVisualBuffer()) {
            const float* ch0 = buffer.getReadPointer(0);
            for (int s = 0; s < numSamples; ++s)
                vb->pushSample(ch0[s]);
        }
    }

    // -------------------------------------------------------------------------
    // Waveform generators
    // -------------------------------------------------------------------------
    float generateSample(int waveform, float phase, float dt) const {
        switch (waveform) {
        case 0:
            return generateSine(phase);
        case 1:
            return generateSquare(phase, dt);
        case 2:
            return generateSaw(phase, dt);
        case 3:
            return generateTriangle(phase, dt);
        default:
            return 0.0f;
        }
    }

    static float generateSine(float phase) { return std::sin(phase * juce::MathConstants<float>::twoPi); }

    static float generateSquare(float phase, float dt) {
        float sample = phase < 0.5f ? 1.0f : -1.0f;
        sample += polyBlep(phase, dt);
        sample -= polyBlep(std::fmod(phase + 0.5f, 1.0f), dt);
        return sample;
    }

    static float generateSaw(float phase, float dt) {
        float sample = 2.0f * phase - 1.0f;
        sample -= polyBlep(phase, dt);
        return sample;
    }

    static float generateTriangle(float phase, float dt) {
        float sample = 4.0f * std::abs(phase - 0.5f) - 1.0f;
        // PolyBLAMP: integrated polyBLEP applied at discontinuities (phase=0 and phase=0.5)
        sample += polyBlamp(phase, dt);
        sample -= polyBlamp(std::fmod(phase + 0.5f, 1.0f), dt);
        return sample;
    }

    static float polyBlamp(float t, float dt) {
        if (t < dt) {
            float n = t / dt;
            // Third-order polynomial: integrated polyBLEP
            return -dt * (n * n * n / 3.0f - n * n / 2.0f + 1.0f / 6.0f) * 4.0f;
        }
        if (t > 1.0f - dt) {
            float n = (t - 1.0f) / dt;
            return dt * (n * n * n / 3.0f + n * n / 2.0f + 1.0f / 6.0f) * 4.0f;
        }
        return 0.0f;
    }

    static float polyBlep(float t, float dt) {
        if (t < dt) {
            float n = t / dt;
            return n + n - n * n - 1.0f;
        }
        if (t > 1.0f - dt) {
            float n = (t - 1.0f) / dt;
            return n * n + n + n + 1.0f;
        }
        return 0.0f;
    }

    // -------------------------------------------------------------------------
    // Member variables
    // -------------------------------------------------------------------------
    double currentSampleRate = 44100.0;

    // Pre-allocated buffers to avoid heap allocation in audio thread
    std::array<std::array<float, 4096>, MAX_VOICES> pitchCVCache{};
    std::array<float, 4096> waveformCVCache{};
    std::array<float, 4096> octaveCVCache{};
    std::array<float, 4096> coarseCVCache{};
    std::array<float, 4096> fineCVCache{};
    std::array<float, 4096> levelCVCache{};

    juce::AudioParameterChoice* waveformParam = nullptr;
    juce::AudioParameterInt* octaveParam = nullptr;
    juce::AudioParameterInt* coarseParam = nullptr;
    juce::AudioParameterFloat* fineParam = nullptr;
    juce::AudioParameterFloat* levelParam = nullptr;
    juce::AudioParameterBool* polyParam = nullptr;
    juce::AudioParameterInt* unisonParam = nullptr;
    juce::AudioParameterFloat* detuneParam = nullptr;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OscillatorModule)
};
