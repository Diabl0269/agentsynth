// ParametricEQModule rendering: preparing the stages, reading the knobs and CV, rewriting coefficients once a block,
// running each enabled band's stages, and the solo audition. docs/modules/fx-modules.md#parametric-eq-module.
#include "Modules/FX/ParametricEQ/ParametricEQFilterDesign.h"
#include "Modules/FX/ParametricEQModule.h"

// Everything the audio thread touches is allocated here: every band's stages for both channels (24 x 8 x 2 filters,
// 24 x 8 coefficient sets), the solo filter and the solo's dry-copy buffer. processModuleBlock only rewrites the
// coefficients' raw values in place. Coefficients are shared between the two channels: juce::dsp::IIR keeps its filter
// state in the Filter, not the Coefficients, so this is safe (it is what ProcessorDuplicator does). Every stage keeps
// the order-2 layout, a first-order stage included, so a Filter never re-sizes its state.
void ParametricEQModule::prepareToPlay(double sampleRate, int samplesPerBlock) {
    prepareOutputStage(sampleRate, samplesPerBlock);
    lastSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;
    const int blockSize = std::max(1, samplesPerBlock);
    juce::dsp::ProcessSpec monoSpec{lastSampleRate, static_cast<juce::uint32>(blockSize), 1};

    for (int b = 0; b < kMaxBands; ++b) {
        for (int s = 0; s < kMaxStages; ++s) {
            coefficients[(size_t)b][(size_t)s] = new Coefs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            for (int ch = 0; ch < kNumAudioChannels; ++ch) {
                auto& filter = filters[(size_t)ch][(size_t)b][(size_t)s];
                filter.prepare(monoSpec);
                filter.coefficients = coefficients[(size_t)b][(size_t)s];
                filter.reset();
            }
        }
        activeStages[(size_t)b] = 0;
    }
    soloCoefficients = new Coefs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
    for (auto& filter : soloFilters) {
        filter.prepare(monoSpec);
        filter.coefficients = soloCoefficients;
        filter.reset();
    }
    soloScratch.setSize(kNumAudioChannels, blockSize, false, true, false);
    appliedSolo = -1;

    // Every band's Freq / Gain / Q is smoothed: swapping a stage's coefficients wholesale puts a step in the output.
    // Snapped to the knobs here so a static render is bit-identical.
    for (int b = 0; b < kMaxBands; ++b) {
        smoothedFreq[(size_t)b].reset(lastSampleRate, 0.02);
        smoothedGain[(size_t)b].reset(lastSampleRate, 0.02);
        smoothedQ[(size_t)b].reset(lastSampleRate, 0.02);
        smoothedFreq[(size_t)b].setCurrentAndTargetValue(bands[(size_t)b].freq->get());
        smoothedGain[(size_t)b].setCurrentAndTargetValue(bands[(size_t)b].gain->get());
        smoothedQ[(size_t)b].setCurrentAndTargetValue(bands[(size_t)b].q->get());
    }
    smoothedOutputGain.reset(lastSampleRate, 0.02);
    smoothedOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputGainParam->get()));

    updateCoefficients();
}

void ParametricEQModule::processModuleBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) {
    // Borrow Left into Right's raw channel while Dual I/O is split and only Left is patched; before the bypass/mute
    // branches so both see a filled Right leg exactly as if the user had cabled it.
    applyLeftRightNormalling(buffer);
    juce::ignoreUnused(midiMessages);

    if (isBypassed()) {
        // Dry pass-through; clear CV channels so mod signals don't leak downstream as audio.
        for (int ch = kNumAudioChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, buffer.getNumSamples());
        return;
    }
    if (isMuted()) {
        buffer.clear();
        return;
    }

    const int numSamples = buffer.getNumSamples();
    const int numAudioCh = std::min(kNumAudioChannels, buffer.getNumChannels());
    if (numSamples == 0 || numAudioCh == 0)
        return;

    readTargetsAndCV(buffer, numSamples);

    // Coefficients update once per block from the smoothed values: with 20 ms smoothing the per-block step is small
    // enough that swapping coefficients wholesale is inaudible, and the inner loops stay plain biquads.
    for (int b = 0; b < kMaxBands; ++b) {
        smoothedFreq[(size_t)b].skip(numSamples);
        smoothedGain[(size_t)b].skip(numSamples);
        smoothedQ[(size_t)b].skip(numSamples);
    }
    updateCoefficients();

    if (soloAudible)
        renderSoloed(buffer, numAudioCh, numSamples);
    else
        filterRange(buffer, numAudioCh, 0, numSamples);

    // Output gain, per sample so the smoothing applies.
    for (int i = 0; i < numSamples; ++i) {
        const float g = smoothedOutputGain.getNextValue();
        for (int ch = 0; ch < numAudioCh; ++ch)
            buffer.getWritePointer(ch)[i] *= g;
    }

    if (auto* vb = getVisualBuffer())
        vb->pushBlock(buffer.getReadPointer(0), numSamples);

    for (int ch = kNumAudioChannels; ch < buffer.getNumChannels(); ++ch)
        buffer.clear(ch, 0, numSamples);
}

// Per-block CV, RMS-gated like the rest of the FX suite. Channels 2-5 drive the two bells (bands 2-3) through their
// bespoke exponential/linear curves; 6-14 drive B1/B4 Freq+Gain, every original band's Q and Output in the normalised
// convention (docs/modules/modulation.md#cv-in-normalised-units). `base` is each parameter's KNOB value, never the
// smoother's own: with no CV patched that would make the target the smoother itself and freeze it. Each active flag
// is published so getBandSnapshots() overlays the CV-resolved value. Bands 5-24 read channels past Pan (knob-only).
void ParametricEQModule::readTargetsAndCV(const juce::AudioBuffer<float>& buffer, int numSamples) {
    for (int b = 0; b < kMaxBands; ++b) {
        smoothedFreq[(size_t)b].setTargetValue(bands[(size_t)b].freq->get());
        smoothedGain[(size_t)b].setTargetValue(bands[(size_t)b].gain->get());
        smoothedQ[(size_t)b].setTargetValue(bands[(size_t)b].q->get());
    }
    for (int i = 0; i < kNumBellBands; ++i) {
        const auto b = (size_t)kBellBandIndex[i];
        const float cvFreq = readCV(buffer, 2 + i * 2, numSamples);
        const float cvGain = readCV(buffer, 3 + i * 2, numSamples);
        smoothedFreq[b].setTargetValue(applyFreqCV(bands[b].freq->get(), cvFreq));
        smoothedGain[b].setTargetValue(applyGainCV(bands[b].gain->get(), cvGain));
        // readCV returns exactly 0 for an unpatched (gated) jack.
        const bool bellActive = (cvFreq != 0.0f || cvGain != 0.0f);
        freqCVActive[b].store(bellActive, std::memory_order_relaxed);
        gainCVActive[b].store(bellActive, std::memory_order_relaxed);
    }

    auto normalised = [&](int band, juce::AudioParameterFloat& param, int channel, auto& smoother, auto& active) {
        const float cv = blockCV(buffer, channel);
        smoother[(size_t)band].setTargetValue(modulateNormalised(param, param.get(), cv));
        active[(size_t)band].store(cv != 0.0f, std::memory_order_relaxed);
    };
    normalised(0, *bands[0].freq, 6, smoothedFreq, freqCVActive);
    normalised(0, *bands[0].gain, 7, smoothedGain, gainCVActive);
    normalised(3, *bands[3].freq, 8, smoothedFreq, freqCVActive);
    normalised(3, *bands[3].gain, 9, smoothedGain, gainCVActive);
    for (int b = 0; b < kOriginalBands; ++b)
        normalised(b, *bands[(size_t)b].q, 10 + b, smoothedQ, qCVActive);
    for (int b = kOriginalBands; b < kMaxBands; ++b) {
        normalised(b, *bands[(size_t)b].freq, bandCvChannel(b, 0), smoothedFreq, freqCVActive);
        normalised(b, *bands[(size_t)b].gain, bandCvChannel(b, 1), smoothedGain, gainCVActive);
        normalised(b, *bands[(size_t)b].q, bandCvChannel(b, 2), smoothedQ, qCVActive);
    }

    smoothedOutputGain.setTargetValue(juce::Decibels::decibelsToGain(
        modulateNormalised(*outputGainParam, outputGainParam->get(), blockCV(buffer, 14))));
}

float ParametricEQModule::readCV(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) noexcept {
    if (channel >= buffer.getNumChannels() || numSamples <= 0)
        return 0.0f;
    const auto* cv = buffer.getReadPointer(channel);
    float sumSq = 0.0f;
    for (int i = 0; i < numSamples; ++i)
        sumSq += cv[i] * cv[i];
    if ((sumSq / static_cast<float>(numSamples)) <= 1.0e-6f)
        return 0.0f;
    return cv[numSamples / 2];
}

// Only enabled bands are designed; a disabled band is skipped by filterRange. A stage past the first that becomes
// active (a steeper slope, a cut chosen, the band switched back on) starts from silence rather than from stale state;
// the first stage keeps its state, as the four-band module's single biquad always did.
void ParametricEQModule::updateCoefficients() {
    float bandFreq[kMaxBands];
    float bandGain[kMaxBands];
    float bandQ[kMaxBands];
    for (int b = 0; b < kMaxBands; ++b) {
        bandFreq[b] = smoothedFreq[(size_t)b].getCurrentValue();
        bandGain[b] = smoothedGain[(size_t)b].getCurrentValue();
        bandQ[b] = smoothedQ[(size_t)b].getCurrentValue();
    }

    synth::eq::BandStages stages{};
    for (int b = 0; b < kMaxBands; ++b) {
        auto& coefs = coefficients[(size_t)b];
        if (coefs[0] == nullptr)
            continue;
        if (!bands[(size_t)b].on->get()) {
            synth::eq::writeUnity(coefs[0]->getRawCoefficients());
            activeStages[(size_t)b] = 0;
            continue;
        }
        const int count =
            synth::eq::designStages(typeOf(b), slopeOf(b), bandFreq[b], bandGain[b], bandQ[b], lastSampleRate, stages);
        for (int s = 0; s < count; ++s)
            std::copy(stages[(size_t)s].begin(), stages[(size_t)s].end(), coefs[(size_t)s]->getRawCoefficients());
        for (int s = std::max(1, activeStages[(size_t)b]); s < count; ++s)
            for (auto& channel : filters)
                channel[(size_t)b][(size_t)s].reset();
        activeStages[(size_t)b] = count;
    }

    for (int b = 0; b < kMaxBands; ++b) {
        resolvedFreq[(size_t)b].store(bandFreq[b], std::memory_order_relaxed);
        resolvedGain[(size_t)b].store(bandGain[b], std::memory_order_relaxed);
        resolvedQ[(size_t)b].store(bandQ[b], std::memory_order_relaxed);
    }
    updateSoloCoefficients(bandFreq, bandQ);
}

// The solo's region: a band-pass at the band's frequency, with the band's Q for a bell, notch, band pass, shelf or
// tilt (what the band moves), and with Q 1/sqrt 2 at a cut's corner (the edge the cut shapes). A newly soloed band's
// filter starts from silence. A solo on a band that is off plays the normal output.
void ParametricEQModule::updateSoloCoefficients(const float* freq, const float* q) {
    const int solo = soloBand.load(std::memory_order_relaxed);
    soloAudible = isValidBand(solo) && bands[(size_t)solo].on->get() && soloCoefficients != nullptr;
    if (!soloAudible) {
        appliedSolo = -1;
        return;
    }
    const auto type = typeOf(solo);
    const float soloQ = synth::eq::isCut(type) ? kDefaultQ : q[solo];
    synth::eq::writeBiquad(BandType::BandPass, freq[solo], 0.0f, soloQ, lastSampleRate,
                           soloCoefficients->getRawCoefficients());
    if (appliedSolo != solo) {
        for (auto& filter : soloFilters)
            filter.reset();
        appliedSolo = solo;
    }
}

// Channel-outer, band-inner, stage-innermost: with one stage per band this is the four-band module's loop exactly.
void ParametricEQModule::filterRange(juce::AudioBuffer<float>& buffer, int numAudioCh, int start, int numSamples) {
    for (int ch = 0; ch < numAudioCh; ++ch) {
        auto* data = buffer.getWritePointer(ch, start);
        for (int b = 0; b < kMaxBands; ++b) {
            if (!bands[(size_t)b].on->get())
                continue; // a disabled band is a straight wire: skip the work entirely
            for (int s = 0; s < activeStages[(size_t)b]; ++s) {
                auto& filter = filters[(size_t)ch][(size_t)b][(size_t)s];
                for (int i = 0; i < numSamples; ++i)
                    data[i] = filter.processSample(data[i]);
                filter.snapToZero();
            }
        }
    }
}

// The bands keep running on the real signal while a band is soloed, so clearing the solo resumes exactly the output
// an unsoloed EQ would give. The solo itself band-passes a dry copy of the input, a scratch block at a time.
void ParametricEQModule::renderSoloed(juce::AudioBuffer<float>& buffer, int numAudioCh, int numSamples) {
    const int chunk = std::max(1, soloScratch.getNumSamples());
    for (int start = 0; start < numSamples; start += chunk) {
        const int n = std::min(chunk, numSamples - start);
        for (int ch = 0; ch < numAudioCh; ++ch)
            soloScratch.copyFrom(ch, 0, buffer, ch, start, n);
        filterRange(buffer, numAudioCh, start, n);
        for (int ch = 0; ch < numAudioCh; ++ch) {
            auto* dry = soloScratch.getWritePointer(ch);
            auto& filter = soloFilters[(size_t)ch];
            for (int i = 0; i < n; ++i)
                dry[i] = filter.processSample(dry[i]);
            filter.snapToZero();
            buffer.copyFrom(ch, start, soloScratch, ch, 0, n);
        }
    }
}
