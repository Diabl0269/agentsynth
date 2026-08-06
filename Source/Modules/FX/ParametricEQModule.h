#pragma once

#include "../ModuleBase.h"
#include <array>
#include <atomic>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

/** Four-band parametric EQ for surgical tone shaping.
 *
 *  Band layout (fixed, matching the classic console strip):
 *    0  Low Shelf   — broad bottom-end lift/cut
 *    1  Peak 1      — fully parametric bell (freq / gain / Q)
 *    2  Peak 2      — fully parametric bell (freq / gain / Q)
 *    3  High Shelf  — broad top-end lift/cut
 *
 *  Channels: 0-1 stereo audio in/out, 2-5 shared CV for the two bells (the musically useful
 *  sweep targets). Shelves are set from their parameters only — consistent with the rest of the
 *  FX suite, which exposes CV for the two or three parameters worth modulating rather than one
 *  jack per knob.
 *
 *  DSP: RBJ cookbook biquads (juce::dsp::IIR::Filter per channel per band). Coefficient objects
 *  are allocated once in prepareToPlay and rewritten in place each block, so processBlock never
 *  touches the heap. Coefficients are shared between the two channels — juce::dsp::IIR keeps its
 *  filter state in the Filter, not the Coefficients, so this is safe (it is what
 *  ProcessorDuplicator does internally).
 *
 *  The analytic magnitude helpers (bandMagnitudeDb / responseDb) use the analog prototypes the
 *  digital coefficients are derived from. EQCurveComponent draws the curve with them, and they
 *  are pure static functions so the response can be unit-tested headlessly.
 */
class ParametricEQModule : public ModuleBase {
public:
    static constexpr int kNumBands = 4;
    static constexpr float kMaxGainDb = 24.0f;
    /** Shelf slope. Q = 1/sqrt(2) is the RBJ "S = 1" case — maximally flat, no shelf overshoot. */
    static constexpr float kShelfQ = 0.70710678f;

    enum class BandType { LowShelf = 0, Peak = 1, HighShelf = 2 };

    /** Resolved (post-CV) settings of one band — what the visualiser draws. */
    struct BandSnapshot {
        BandType type = BandType::Peak;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = kShelfQ;
    };

    ParametricEQModule()
        : ModuleBase("Parametric EQ", 6, 2) { // 0-1 audio, 2-5 CV (B1 Freq/Gain, B2 Freq/Gain)
        addParameter(lowFreqParam = new juce::AudioParameterFloat("lowFreq", "Low Freq (Hz)",
                                                                  freqRange(20.0f, 1000.0f, 150.0f), 120.0f));
        addParameter(lowGainParam =
                         new juce::AudioParameterFloat("lowGain", "Low Gain (dB)", -kMaxGainDb, kMaxGainDb, 0.0f));

        addParameter(band1FreqParam = new juce::AudioParameterFloat("band1Freq", "B1 Freq (Hz)",
                                                                    freqRange(40.0f, 8000.0f, 600.0f), 500.0f));
        addParameter(band1GainParam =
                         new juce::AudioParameterFloat("band1Gain", "B1 Gain (dB)", -kMaxGainDb, kMaxGainDb, 0.0f));
        addParameter(band1QParam = new juce::AudioParameterFloat("band1Q", "B1 Q", qRange(), 0.707f));

        addParameter(band2FreqParam = new juce::AudioParameterFloat("band2Freq", "B2 Freq (Hz)",
                                                                    freqRange(200.0f, 16000.0f, 2000.0f), 3000.0f));
        addParameter(band2GainParam =
                         new juce::AudioParameterFloat("band2Gain", "B2 Gain (dB)", -kMaxGainDb, kMaxGainDb, 0.0f));
        addParameter(band2QParam = new juce::AudioParameterFloat("band2Q", "B2 Q", qRange(), 0.707f));

        addParameter(highFreqParam = new juce::AudioParameterFloat("highFreq", "High Freq (Hz)",
                                                                   freqRange(1000.0f, 20000.0f, 6000.0f), 8000.0f));
        addParameter(highGainParam =
                         new juce::AudioParameterFloat("highGain", "High Gain (dB)", -kMaxGainDb, kMaxGainDb, 0.0f));

        addParameter(outputGainParam =
                         new juce::AudioParameterFloat("outputGain", "Output (dB)", -kMaxGainDb, kMaxGainDb, 0.0f));
        addMuteParameter();
        enableVisualBuffer(true);

        publishSnapshot(); // so the UI has sane values before the first processBlock
    }

    void prepareToPlay(double sampleRate, int samplesPerBlock) override {
        lastSampleRate = (sampleRate > 0.0) ? sampleRate : 44100.0;

        juce::dsp::ProcessSpec monoSpec{lastSampleRate, static_cast<juce::uint32>(std::max(1, samplesPerBlock)), 1};

        for (int b = 0; b < kNumBands; ++b) {
            // Allocate once here; processBlock only ever rewrites the raw values in place.
            coefficients[(size_t)b] = new Coefs(1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f);
            for (int ch = 0; ch < kNumAudioChannels; ++ch) {
                auto& filter = filters[(size_t)ch][(size_t)b];
                filter.prepare(monoSpec);
                filter.coefficients = coefficients[(size_t)b];
                filter.reset();
            }
        }

        smoothedBand1Freq.reset(lastSampleRate, 0.02);
        smoothedBand2Freq.reset(lastSampleRate, 0.02);
        smoothedBand1Gain.reset(lastSampleRate, 0.02);
        smoothedBand2Gain.reset(lastSampleRate, 0.02);
        smoothedOutputGain.reset(lastSampleRate, 0.02);

        smoothedBand1Freq.setCurrentAndTargetValue(*band1FreqParam);
        smoothedBand2Freq.setCurrentAndTargetValue(*band2FreqParam);
        smoothedBand1Gain.setCurrentAndTargetValue(*band1GainParam);
        smoothedBand2Gain.setCurrentAndTargetValue(*band2GainParam);
        smoothedOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputGainParam->get()));

        updateCoefficients(*band1FreqParam, *band1GainParam, *band2FreqParam, *band2GainParam);
    }

    void processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages) override {
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

        // ---- CV (sampled once per block, RMS-gated like the rest of the FX suite) ----
        const float cvBand1Freq = readCV(buffer, 2, numSamples);
        const float cvBand1Gain = readCV(buffer, 3, numSamples);
        const float cvBand2Freq = readCV(buffer, 4, numSamples);
        const float cvBand2Gain = readCV(buffer, 5, numSamples);

        const auto band1Range = band1FreqParam->getNormalisableRange();
        const auto band2Range = band2FreqParam->getNormalisableRange();

        smoothedBand1Freq.setTargetValue(applyFreqCV(*band1FreqParam, cvBand1Freq, band1Range.start, band1Range.end));
        smoothedBand2Freq.setTargetValue(applyFreqCV(*band2FreqParam, cvBand2Freq, band2Range.start, band2Range.end));
        smoothedBand1Gain.setTargetValue(applyGainCV(*band1GainParam, cvBand1Gain));
        smoothedBand2Gain.setTargetValue(applyGainCV(*band2GainParam, cvBand2Gain));
        smoothedOutputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainParam->get()));

        // Coefficients update once per block from the smoothed values. With 20 ms smoothing the
        // per-block step is small enough that swapping coefficients wholesale is inaudible, and
        // it keeps the inner loops to a plain biquad.
        smoothedBand1Freq.skip(numSamples);
        smoothedBand2Freq.skip(numSamples);
        smoothedBand1Gain.skip(numSamples);
        smoothedBand2Gain.skip(numSamples);

        updateCoefficients(smoothedBand1Freq.getCurrentValue(), smoothedBand1Gain.getCurrentValue(),
                           smoothedBand2Freq.getCurrentValue(), smoothedBand2Gain.getCurrentValue());

        // ---- Filter the audio channels ----
        for (int ch = 0; ch < numAudioCh; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int b = 0; b < kNumBands; ++b) {
                auto& filter = filters[(size_t)ch][(size_t)b];
                for (int i = 0; i < numSamples; ++i)
                    data[i] = filter.processSample(data[i]);
                filter.snapToZero();
            }
        }

        // ---- Output gain (per-sample so the smoothing actually applies) ----
        for (int i = 0; i < numSamples; ++i) {
            const float g = smoothedOutputGain.getNextValue();
            for (int ch = 0; ch < numAudioCh; ++ch)
                buffer.getWritePointer(ch)[i] *= g;
        }

        if (auto* vb = getVisualBuffer()) {
            const auto* ch0 = buffer.getReadPointer(0);
            for (int i = 0; i < numSamples; ++i)
                vb->pushSample(ch0[i]);
        }

        // Clear CV channels to prevent leaking to downstream modules
        for (int ch = kNumAudioChannels; ch < buffer.getNumChannels(); ++ch)
            buffer.clear(ch, 0, numSamples);
    }

    juce::String getInputPortLabel(int i) const override {
        const juce::String labels[] = {"Left", "Right", "B1 Freq", "B1 Gain", "B2 Freq", "B2 Gain"};
        return (i >= 0 && i < 6) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"B1 Freq", 2}, {"B1 Gain", 3}, {"B2 Freq", 4}, {"B2 Gain", 5}};
    }
    ModulationCategory getModulationCategory() const override { return ModulationCategory::Filter; }
    ModuleType getModuleType() const override { return ModuleType::ParametricEQ; }

    LogicalPort mapInputChannel(int raw) const override {
        LogicalPort p;
        if (raw >= 0 && raw < 6) {
            p.visibleJackIndex = raw;
            p.role = (raw < kNumAudioChannels) ? PortRole::Audio : PortRole::ModCV;
            p.isPolyGroupHead = true;
            p.polyVoiceSpan = 1;
            return p;
        }
        return ModuleBase::mapInputChannel(raw);
    }

    // ---------- visualiser accessors (message thread) ----------

    /** Resolved band settings including CV, as of the last processed block. */
    std::array<BandSnapshot, kNumBands> getBandSnapshots() const {
        std::array<BandSnapshot, kNumBands> out{};
        for (int b = 0; b < kNumBands; ++b) {
            out[(size_t)b].type = bandTypeFor(b);
            out[(size_t)b].freqHz = displayFreq[(size_t)b].load(std::memory_order_relaxed);
            out[(size_t)b].gainDb = displayGain[(size_t)b].load(std::memory_order_relaxed);
            out[(size_t)b].q = displayQ[(size_t)b].load(std::memory_order_relaxed);
        }
        return out;
    }

    float getOutputGainDb() const { return outputGainParam->get(); }
    double getLastSampleRate() const { return lastSampleRate; }

    /** Which band type sits at `index` (0=LowShelf, 1/2=Peak, 3=HighShelf). */
    static BandType bandTypeFor(int index) noexcept {
        if (index == 0)
            return BandType::LowShelf;
        if (index == kNumBands - 1)
            return BandType::HighShelf;
        return BandType::Peak;
    }

    // ---------- pure static helpers (unit-testable without a graph or a GUI) ----------

    /** Magnitude in dB of one band's analog prototype at `freq`.
     *  Returns 0 dB far from the band's action, and exactly `gainDb` where the band is fully
     *  effective (DC for a low shelf, the centre for a bell, Nyquist-ward for a high shelf).
     */
    static float bandMagnitudeDb(BandType type, float centreHz, float gainDb, float q, float freq) noexcept {
        if (centreHz <= 0.0f || freq <= 0.0f || !std::isfinite(centreHz) || !std::isfinite(freq))
            return 0.0f;

        const float A = std::pow(10.0f, gainDb / 40.0f);
        const float Q = std::max(0.05f, q);
        const float w = freq / centreHz;
        const float w2 = w * w;
        const float sqrtA = std::sqrt(A);

        float numSq = 1.0f;
        float denSq = 1.0f;

        switch (type) {
        case BandType::Peak: {
            // H(s) = (s^2 + (A/Q)s + 1) / (s^2 + s/(A*Q) + 1),  s = jw,  w = f/f0
            const float base = (1.0f - w2) * (1.0f - w2);
            const float numImag = A * w / Q;
            const float denImag = w / (A * Q);
            numSq = base + numImag * numImag;
            denSq = base + denImag * denImag;
            break;
        }
        case BandType::LowShelf: {
            // H(s) = A * (s^2 + (sqrt(A)/Q)s + A) / (A*s^2 + (sqrt(A)/Q)s + 1)
            const float imag = w * sqrtA / Q;
            numSq = A * A * ((A - w2) * (A - w2) + imag * imag);
            denSq = (1.0f - A * w2) * (1.0f - A * w2) + imag * imag;
            break;
        }
        case BandType::HighShelf: {
            // H(s) = A * (A*s^2 + (sqrt(A)/Q)s + 1) / (s^2 + (sqrt(A)/Q)s + A)
            const float imag = w * sqrtA / Q;
            numSq = A * A * ((1.0f - A * w2) * (1.0f - A * w2) + imag * imag);
            denSq = (A - w2) * (A - w2) + imag * imag;
            break;
        }
        }

        if (!(denSq > 0.0f))
            return 0.0f;
        // 10*log10 of a squared magnitude ratio == 20*log10 of the magnitude ratio.
        return 10.0f * std::log10(std::max(numSq / denSq, 1.0e-12f));
    }

    /** Combined response of all bands plus the output trim, in dB, at `freq`. */
    static float responseDb(const std::array<BandSnapshot, kNumBands>& bands, float outputGainDb, float freq) noexcept {
        float total = outputGainDb;
        for (const auto& band : bands)
            total += bandMagnitudeDb(band.type, band.freqHz, band.gainDb, band.q, freq);
        return total;
    }

    /** Writes RBJ cookbook biquad coefficients for one band into `dest[0..4]`, normalised the way
     *  juce::dsp::IIR::Coefficients stores them: {b0/a0, b1/a0, b2/a0, a1/a0, a2/a0}.
     *  `dest` must have room for 5 floats.
     */
    static void writeBiquad(BandType type, float centreHz, float gainDb, float q, double sampleRate,
                            float* dest) noexcept {
        const double fs = (sampleRate > 0.0) ? sampleRate : 44100.0;
        // Keep the pole away from Nyquist, where the bilinear transform degenerates.
        const double f0 = juce::jlimit(1.0, fs * 0.49, static_cast<double>(centreHz));
        const double A = std::pow(10.0, static_cast<double>(gainDb) / 40.0);
        const double Q = std::max(0.05, static_cast<double>(q));
        const double w0 = 2.0 * juce::MathConstants<double>::pi * f0 / fs;
        const double cosW0 = std::cos(w0);
        const double alpha = std::sin(w0) / (2.0 * Q);
        const double sqrtA = std::sqrt(A);

        double b0 = 1.0, b1 = 0.0, b2 = 0.0, a0 = 1.0, a1 = 0.0, a2 = 0.0;

        switch (type) {
        case BandType::Peak:
            b0 = 1.0 + alpha * A;
            b1 = -2.0 * cosW0;
            b2 = 1.0 - alpha * A;
            a0 = 1.0 + alpha / A;
            a1 = -2.0 * cosW0;
            a2 = 1.0 - alpha / A;
            break;
        case BandType::LowShelf:
            b0 = A * ((A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            b1 = 2.0 * A * ((A - 1.0) - (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            a0 = (A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cosW0);
            a2 = (A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
            break;
        case BandType::HighShelf:
            b0 = A * ((A + 1.0) + (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha);
            b1 = -2.0 * A * ((A - 1.0) + (A + 1.0) * cosW0);
            b2 = A * ((A + 1.0) + (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha);
            a0 = (A + 1.0) - (A - 1.0) * cosW0 + 2.0 * sqrtA * alpha;
            a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cosW0);
            a2 = (A + 1.0) - (A - 1.0) * cosW0 - 2.0 * sqrtA * alpha;
            break;
        }

        const double inv = (std::abs(a0) > 1.0e-12) ? 1.0 / a0 : 1.0;
        dest[0] = static_cast<float>(b0 * inv);
        dest[1] = static_cast<float>(b1 * inv);
        dest[2] = static_cast<float>(b2 * inv);
        dest[3] = static_cast<float>(a1 * inv);
        dest[4] = static_cast<float>(a2 * inv);
    }

    /** Exponential CV mapping: cv=+1 sweeps the band up to `hi`, cv=-1 down to `lo`.
     *  Matches FilterModule's cutoff-CV feel so a single LFO drives both the same way.
     */
    static float applyFreqCV(float base, float cv, float lo, float hi) noexcept {
        base = juce::jlimit(lo, hi, base);
        cv = juce::jlimit(-1.0f, 1.0f, cv);
        if (base <= 0.0f || lo <= 0.0f || cv == 0.0f)
            return base;
        if (cv > 0.0f)
            return juce::jlimit(lo, hi, base * std::pow(hi / base, cv));
        return juce::jlimit(lo, hi, base * std::pow(lo / base, -cv));
    }

    /** CV maps linearly onto the band's full +/-24 dB range and adds to the knob value. */
    static float applyGainCV(float baseDb, float cv) noexcept {
        cv = juce::jlimit(-1.0f, 1.0f, cv);
        return juce::jlimit(-kMaxGainDb, kMaxGainDb, baseDb + cv * kMaxGainDb);
    }

private:
    static constexpr int kNumAudioChannels = 2;

    using Coefs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    static juce::NormalisableRange<float> freqRange(float lo, float hi, float centre) {
        juce::NormalisableRange<float> range(lo, hi);
        range.setSkewForCentre(centre);
        return range;
    }

    static juce::NormalisableRange<float> qRange() {
        juce::NormalisableRange<float> range(0.1f, 10.0f);
        range.setSkewForCentre(1.0f);
        return range;
    }

    /** Per-block CV sample, gated on RMS so an unconnected (silent) jack reads as exactly 0. */
    static float readCV(const juce::AudioBuffer<float>& buffer, int channel, int numSamples) noexcept {
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

    void updateCoefficients(float band1Freq, float band1Gain, float band2Freq, float band2Gain) {
        const float bandFreq[kNumBands] = {lowFreqParam->get(), band1Freq, band2Freq, highFreqParam->get()};
        const float bandGain[kNumBands] = {lowGainParam->get(), band1Gain, band2Gain, highGainParam->get()};
        const float bandQ[kNumBands] = {kShelfQ, band1QParam->get(), band2QParam->get(), kShelfQ};

        for (int b = 0; b < kNumBands; ++b) {
            if (coefficients[(size_t)b] == nullptr)
                continue;
            writeBiquad(bandTypeFor(b), bandFreq[b], bandGain[b], bandQ[b], lastSampleRate,
                        coefficients[(size_t)b]->getRawCoefficients());
            displayFreq[(size_t)b].store(bandFreq[b], std::memory_order_relaxed);
            displayGain[(size_t)b].store(bandGain[b], std::memory_order_relaxed);
            displayQ[(size_t)b].store(bandQ[b], std::memory_order_relaxed);
        }
    }

    /** Seeds the display atomics from the raw parameters (no CV, no smoothing). */
    void publishSnapshot() {
        const float bandFreq[kNumBands] = {lowFreqParam->get(), band1FreqParam->get(), band2FreqParam->get(),
                                           highFreqParam->get()};
        const float bandGain[kNumBands] = {lowGainParam->get(), band1GainParam->get(), band2GainParam->get(),
                                           highGainParam->get()};
        const float bandQ[kNumBands] = {kShelfQ, band1QParam->get(), band2QParam->get(), kShelfQ};
        for (int b = 0; b < kNumBands; ++b) {
            displayFreq[(size_t)b].store(bandFreq[b], std::memory_order_relaxed);
            displayGain[(size_t)b].store(bandGain[b], std::memory_order_relaxed);
            displayQ[(size_t)b].store(bandQ[b], std::memory_order_relaxed);
        }
    }

    std::array<std::array<Filter, kNumBands>, kNumAudioChannels> filters;
    std::array<Coefs::Ptr, kNumBands> coefficients;

    double lastSampleRate = 44100.0;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedBand1Freq{500.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> smoothedBand2Freq{3000.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBand1Gain{0.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedBand2Gain{0.0f};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain{1.0f};

    juce::AudioParameterFloat* lowFreqParam = nullptr;
    juce::AudioParameterFloat* lowGainParam = nullptr;
    juce::AudioParameterFloat* band1FreqParam = nullptr;
    juce::AudioParameterFloat* band1GainParam = nullptr;
    juce::AudioParameterFloat* band1QParam = nullptr;
    juce::AudioParameterFloat* band2FreqParam = nullptr;
    juce::AudioParameterFloat* band2GainParam = nullptr;
    juce::AudioParameterFloat* band2QParam = nullptr;
    juce::AudioParameterFloat* highFreqParam = nullptr;
    juce::AudioParameterFloat* highGainParam = nullptr;
    juce::AudioParameterFloat* outputGainParam = nullptr;

    // Written from the audio thread each block, read by EQCurveComponent on the message thread.
    std::array<std::atomic<float>, kNumBands> displayFreq{};
    std::array<std::atomic<float>, kNumBands> displayGain{};
    std::array<std::atomic<float>, kNumBands> displayQ{};
};
