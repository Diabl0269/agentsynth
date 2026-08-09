#pragma once

#include "../ModuleBase.h"
#include <array>
#include <atomic>
#include <cmath>
#include <juce_dsp/juce_dsp.h>

/** Four-band parametric EQ for surgical tone shaping, in the traditional console/DAW idiom.
 *
 *  Band slots (fixed types, as in Cubase's channel EQ):
 *    1  Low Shelf   — broad bottom-end lift/cut
 *    2  Peak        — fully parametric bell
 *    3  Peak        — fully parametric bell
 *    4  High Shelf  — broad top-end lift/cut
 *
 *  **All four bands start disabled**, so a freshly dropped EQ is a straight wire and the curve
 *  starts empty. Points are added by double-clicking the response curve (EQCurveComponent), which
 *  enables whichever slot best fits the clicked frequency — see findBandForNewPoint().
 *
 *  Every band exposes Freq / Gain / Q uniformly, so the same drag-and-scroll gestures work on all
 *  of them; the only difference between slots is the shape their type gives them.
 *
 *  DSP: RBJ cookbook biquads (juce::dsp::IIR::Filter per channel per band). Coefficient objects
 *  are allocated once in prepareToPlay and rewritten in place each block, so processBlock never
 *  touches the heap. Coefficients are shared between the two channels — juce::dsp::IIR keeps its
 *  filter state in the Filter, not the Coefficients, so this is safe (it is what
 *  ProcessorDuplicator does internally). A disabled band is written as a literal unity biquad.
 *
 *  The analytic magnitude helpers (bandMagnitudeDb / responseDb) use the analog prototypes the
 *  digital coefficients are derived from. EQCurveComponent draws the curve with them, and they
 *  are pure static functions so the response can be unit-tested headlessly.
 */
class ParametricEQModule : public ModuleBase {
public:
    static constexpr int kNumBands = 4;
    static constexpr float kMaxGainDb = 24.0f;
    static constexpr float kMinFreq = 20.0f;
    static constexpr float kMaxFreq = 20000.0f;
    static constexpr float kMinQ = 0.1f;
    static constexpr float kMaxQ = 10.0f;
    /** Default shelf/bell slope. Q = 1/sqrt(2) is the RBJ "S = 1" case — maximally flat. */
    static constexpr float kDefaultQ = 0.70710678f;

    enum class BandType { LowShelf = 0, Peak = 1, HighShelf = 2 };

    /** Resolved (post-CV) settings of one band — what the visualiser draws. */
    struct BandSnapshot {
        BandType type = BandType::Peak;
        bool enabled = false;
        float freqHz = 1000.0f;
        float gainDb = 0.0f;
        float q = kDefaultQ;
    };

    ParametricEQModule()
        : ModuleBase("Parametric EQ", 6, 2) { // 0-1 audio, 2-5 CV (B2 Freq/Gain, B3 Freq/Gain)
        // Parameters are grouped band-by-band so the custom module layout can walk them in rows.
        // The on/off parameter's display name doubles as the row's label, which is why it reads
        // "1 Low Shelf" rather than "Band 1 On".
        for (int b = 0; b < kNumBands; ++b) {
            const juce::String id = "band" + juce::String(b + 1);
            const juce::String prefix = "B" + juce::String(b + 1) + " ";

            addParameter(bands[(size_t)b].on = new juce::AudioParameterBool(id + "On", rowLabelFor(b), false));
            addParameter(bands[(size_t)b].freq = new juce::AudioParameterFloat(
                             id + "Freq", prefix + "Freq", freqRange(), defaultFreqFor(b), hzAttributes()));
            addParameter(bands[(size_t)b].gain = new juce::AudioParameterFloat(id + "Gain", prefix + "Gain",
                                                                               gainRange(), 0.0f, dbAttributes()));
            addParameter(bands[(size_t)b].q = new juce::AudioParameterFloat(id + "Q", prefix + "Q", qRange(), kDefaultQ,
                                                                            plainAttributes(2)));
        }

        addParameter(outputGainParam =
                         new juce::AudioParameterFloat("outputGain", "Output", gainRange(), 0.0f, dbAttributes()));
        addMuteParameter();
        enableVisualBuffer(true);
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

        smoothedBellFreq[0].reset(lastSampleRate, 0.02);
        smoothedBellFreq[1].reset(lastSampleRate, 0.02);
        smoothedBellGain[0].reset(lastSampleRate, 0.02);
        smoothedBellGain[1].reset(lastSampleRate, 0.02);
        smoothedOutputGain.reset(lastSampleRate, 0.02);

        for (int i = 0; i < kNumBellBands; ++i) {
            smoothedBellFreq[(size_t)i].setCurrentAndTargetValue(bands[(size_t)kBellBandIndex[i]].freq->get());
            smoothedBellGain[(size_t)i].setCurrentAndTargetValue(bands[(size_t)kBellBandIndex[i]].gain->get());
        }
        smoothedOutputGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(outputGainParam->get()));

        updateCoefficients(smoothedBellFreq[0].getCurrentValue(), smoothedBellGain[0].getCurrentValue(),
                           smoothedBellFreq[1].getCurrentValue(), smoothedBellGain[1].getCurrentValue());
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
        // Channels 2-5 modulate the two bell bands: freq then gain, bell 1 then bell 2.
        for (int i = 0; i < kNumBellBands; ++i) {
            const auto& band = bands[(size_t)kBellBandIndex[i]];
            const float cvFreq = readCV(buffer, 2 + i * 2, numSamples);
            const float cvGain = readCV(buffer, 3 + i * 2, numSamples);
            smoothedBellFreq[(size_t)i].setTargetValue(applyFreqCV(band.freq->get(), cvFreq));
            smoothedBellGain[(size_t)i].setTargetValue(applyGainCV(band.gain->get(), cvGain));
            // readCV returns exactly 0 for an unpatched (gated) jack, so this also tells the
            // visualiser whether to show the CV-modulated value or the plain knob value.
            bellCVActive[(size_t)i].store(cvFreq != 0.0f || cvGain != 0.0f, std::memory_order_relaxed);
        }
        smoothedOutputGain.setTargetValue(juce::Decibels::decibelsToGain(outputGainParam->get()));

        // Coefficients update once per block from the smoothed values. With 20 ms smoothing the
        // per-block step is small enough that swapping coefficients wholesale is inaudible, and
        // it keeps the inner loops to a plain biquad.
        for (int i = 0; i < kNumBellBands; ++i) {
            smoothedBellFreq[(size_t)i].skip(numSamples);
            smoothedBellGain[(size_t)i].skip(numSamples);
        }

        updateCoefficients(smoothedBellFreq[0].getCurrentValue(), smoothedBellGain[0].getCurrentValue(),
                           smoothedBellFreq[1].getCurrentValue(), smoothedBellGain[1].getCurrentValue());

        // ---- Filter the audio channels ----
        for (int ch = 0; ch < numAudioCh; ++ch) {
            auto* data = buffer.getWritePointer(ch);
            for (int b = 0; b < kNumBands; ++b) {
                if (!bands[(size_t)b].on->get())
                    continue; // a disabled band is a straight wire — skip the work entirely
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
        const juce::String labels[] = {"Left", "Right", "B2 Freq", "B2 Gain", "B3 Freq", "B3 Gain"};
        return (i >= 0 && i < 6) ? labels[i] : ModuleBase::getInputPortLabel(i);
    }
    juce::String getOutputPortLabel(int i) const override { return i == 0 ? "Left" : "Right"; }

    std::vector<ModulationTarget> getModulationTargets() const override {
        return {{"B2 Freq", 2}, {"B2 Gain", 3}, {"B3 Freq", 4}, {"B3 Gain", 5}};
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

    // ---------- visualiser accessors / mutators (message thread) ----------

    /** Band settings as the visualiser should draw them.
     *
     *  Values come from the parameters, so an edit is visible immediately — the curve must track
     *  a dragged point even when no audio is flowing through the module. The two bell bands then
     *  have their live CV-modulated frequency/gain overlaid, but only while CV is actually
     *  driving them; otherwise the knob value is the effective value.
     */
    std::array<BandSnapshot, kNumBands> getBandSnapshots() const {
        std::array<BandSnapshot, kNumBands> out{};
        for (int b = 0; b < kNumBands; ++b) {
            out[(size_t)b].type = bandTypeFor(b);
            out[(size_t)b].enabled = bands[(size_t)b].on->get();
            out[(size_t)b].freqHz = bands[(size_t)b].freq->get();
            out[(size_t)b].gainDb = bands[(size_t)b].gain->get();
            out[(size_t)b].q = bands[(size_t)b].q->get();
        }
        for (int i = 0; i < kNumBellBands; ++i) {
            if (!bellCVActive[(size_t)i].load(std::memory_order_relaxed))
                continue;
            auto& bell = out[(size_t)kBellBandIndex[i]];
            bell.freqHz = bellCVFreq[(size_t)i].load(std::memory_order_relaxed);
            bell.gainDb = bellCVGain[(size_t)i].load(std::memory_order_relaxed);
        }
        return out;
    }

    bool isBandEnabled(int band) const { return isValidBand(band) && bands[(size_t)band].on->get(); }
    int getEnabledBandCount() const {
        int count = 0;
        for (int b = 0; b < kNumBands; ++b)
            if (bands[(size_t)b].on->get())
                ++count;
        return count;
    }

    // Setters take real-world units and notify the host, so attached sliders in the module card
    // follow along when the user drags a point on the curve.
    void setBandEnabled(int band, bool enabled) {
        if (isValidBand(band))
            bands[(size_t)band].on->setValueNotifyingHost(enabled ? 1.0f : 0.0f);
    }
    void setBandFreq(int band, float freqHz) {
        if (isValidBand(band))
            setFloat(bands[(size_t)band].freq, juce::jlimit(kMinFreq, kMaxFreq, freqHz));
    }
    void setBandGain(int band, float gainDb) {
        if (isValidBand(band))
            setFloat(bands[(size_t)band].gain, juce::jlimit(-kMaxGainDb, kMaxGainDb, gainDb));
    }
    void setBandQ(int band, float q) {
        if (isValidBand(band))
            setFloat(bands[(size_t)band].q, juce::jlimit(kMinQ, kMaxQ, q));
    }

    /** Which slot a new point at `freqHz` should occupy: the disabled band whose home frequency
     *  is closest on a log axis, so a click down low lands on the low shelf and one up top on the
     *  high shelf. Returns -1 when all four slots are already in use.
     */
    int findBandForNewPoint(float freqHz) const {
        int best = -1;
        float bestDistance = 0.0f;
        const float target = std::log(juce::jlimit(kMinFreq, kMaxFreq, freqHz));
        for (int b = 0; b < kNumBands; ++b) {
            if (bands[(size_t)b].on->get())
                continue;
            const float distance = std::abs(target - std::log(defaultFreqFor(b)));
            if (best < 0 || distance < bestDistance) {
                best = b;
                bestDistance = distance;
            }
        }
        return best;
    }

    float getOutputGainDb() const { return outputGainParam->get(); }
    double getLastSampleRate() const { return lastSampleRate; }

    /** Which band type sits in slot `index` (0=LowShelf, 1/2=Peak, 3=HighShelf). */
    static BandType bandTypeFor(int index) noexcept {
        if (index == 0)
            return BandType::LowShelf;
        if (index == kNumBands - 1)
            return BandType::HighShelf;
        return BandType::Peak;
    }

    /** Row label / on-off toggle text for slot `index`, e.g. "1 Low Shelf". */
    static juce::String rowLabelFor(int index) {
        switch (bandTypeFor(index)) {
        case BandType::LowShelf:
            return juce::String(index + 1) + " Low Shelf";
        case BandType::HighShelf:
            return juce::String(index + 1) + " High Shelf";
        case BandType::Peak:
            break;
        }
        return juce::String(index + 1) + " Peak";
    }

    /** Home frequency of slot `index` — its default, and what findBandForNewPoint ranks against. */
    static float defaultFreqFor(int index) noexcept {
        constexpr float defaults[kNumBands] = {100.0f, 500.0f, 3000.0f, 8000.0f};
        return defaults[juce::jlimit(0, kNumBands - 1, index)];
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

    /** Combined response of the ENABLED bands plus the output trim, in dB, at `freq`.
     *  Disabled bands contribute nothing, so an all-off EQ is flat at the trim value.
     */
    static float responseDb(const std::array<BandSnapshot, kNumBands>& bands, float outputGainDb, float freq) noexcept {
        float total = outputGainDb;
        for (const auto& band : bands)
            if (band.enabled)
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

    /** Exponential CV mapping over the full 20 Hz - 20 kHz band range: cv=+1 sweeps the band to
     *  20 kHz, cv=-1 down to 20 Hz. Matches FilterModule's cutoff-CV feel so a single LFO drives
     *  both the same way.
     */
    static float applyFreqCV(float base, float cv) noexcept {
        base = juce::jlimit(kMinFreq, kMaxFreq, base);
        cv = juce::jlimit(-1.0f, 1.0f, cv);
        if (cv == 0.0f)
            return base;
        if (cv > 0.0f)
            return juce::jlimit(kMinFreq, kMaxFreq, base * std::pow(kMaxFreq / base, cv));
        return juce::jlimit(kMinFreq, kMaxFreq, base * std::pow(kMinFreq / base, -cv));
    }

    /** CV maps linearly onto the band's full +/-24 dB range and adds to the knob value. */
    static float applyGainCV(float baseDb, float cv) noexcept {
        cv = juce::jlimit(-1.0f, 1.0f, cv);
        return juce::jlimit(-kMaxGainDb, kMaxGainDb, baseDb + cv * kMaxGainDb);
    }

private:
    static constexpr int kNumAudioChannels = 2;
    /** The two Peak slots are the CV-modulated ones — the musically useful sweep targets. */
    static constexpr int kNumBellBands = 2;
    static constexpr int kBellBandIndex[kNumBellBands] = {1, 2};

    using Coefs = juce::dsp::IIR::Coefficients<float>;
    using Filter = juce::dsp::IIR::Filter<float>;

    struct BandParams {
        juce::AudioParameterBool* on = nullptr;
        juce::AudioParameterFloat* freq = nullptr;
        juce::AudioParameterFloat* gain = nullptr;
        juce::AudioParameterFloat* q = nullptr;
    };

    static bool isValidBand(int band) noexcept { return band >= 0 && band < kNumBands; }

    static void setFloat(juce::AudioParameterFloat* param, float value) {
        if (param != nullptr)
            param->setValueNotifyingHost(param->convertTo0to1(value));
    }

    static juce::NormalisableRange<float> freqRange() {
        juce::NormalisableRange<float> range(kMinFreq, kMaxFreq);
        range.setSkewForCentre(1000.0f);
        return range;
    }

    static juce::NormalisableRange<float> gainRange() { return {-kMaxGainDb, kMaxGainDb}; }

    static juce::NormalisableRange<float> qRange() {
        juce::NormalisableRange<float> range(kMinQ, kMaxQ);
        range.setSkewForCentre(1.0f);
        return range;
    }

    // Readout formatting. Without these the skewed ranges surface values like "2999.9" and
    // "0.7071". Kept compact deliberately: these share the standard 50px knob text box with every
    // other module, so "3.0k" fits where "3.00 kHz" would be truncated. The knob's own label
    // ("B2 Freq" / "B2 Gain") already carries the unit.
    static juce::AudioParameterFloatAttributes hzAttributes() {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction([](float v, int) {
            if (v >= 10000.0f)
                return juce::String(juce::roundToInt(v / 1000.0f)) + "k";
            if (v >= 1000.0f)
                return juce::String(v / 1000.0f, 1) + "k";
            return juce::String(juce::roundToInt(v));
        });
    }

    static juce::AudioParameterFloatAttributes dbAttributes() {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [](float v, int) { return juce::String(v, 1); });
    }

    static juce::AudioParameterFloatAttributes plainAttributes(int decimals) {
        return juce::AudioParameterFloatAttributes().withStringFromValueFunction(
            [decimals](float v, int) { return juce::String(v, decimals); });
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

    void updateCoefficients(float bell1Freq, float bell1Gain, float bell2Freq, float bell2Gain) {
        float bandFreq[kNumBands];
        float bandGain[kNumBands];
        float bandQ[kNumBands];
        for (int b = 0; b < kNumBands; ++b) {
            bandFreq[b] = bands[(size_t)b].freq->get();
            bandGain[b] = bands[(size_t)b].gain->get();
            bandQ[b] = bands[(size_t)b].q->get();
        }
        // The two bells take their values from the CV-smoothed path instead of the raw parameter.
        bandFreq[kBellBandIndex[0]] = bell1Freq;
        bandGain[kBellBandIndex[0]] = bell1Gain;
        bandFreq[kBellBandIndex[1]] = bell2Freq;
        bandGain[kBellBandIndex[1]] = bell2Gain;

        for (int b = 0; b < kNumBands; ++b) {
            if (coefficients[(size_t)b] == nullptr)
                continue;
            if (bands[(size_t)b].on->get())
                writeBiquad(bandTypeFor(b), bandFreq[b], bandGain[b], bandQ[b], lastSampleRate,
                            coefficients[(size_t)b]->getRawCoefficients());
            else
                writeUnity(coefficients[(size_t)b]->getRawCoefficients());
        }

        // Publish the CV-resolved bell values for the visualiser to overlay.
        for (int i = 0; i < kNumBellBands; ++i) {
            bellCVFreq[(size_t)i].store(bandFreq[kBellBandIndex[i]], std::memory_order_relaxed);
            bellCVGain[(size_t)i].store(bandGain[kBellBandIndex[i]], std::memory_order_relaxed);
        }
    }

    /** A pass-through biquad: b == a, so H(z) == 1 at every frequency. */
    static void writeUnity(float* dest) noexcept {
        dest[0] = 1.0f;
        dest[1] = 0.0f;
        dest[2] = 0.0f;
        dest[3] = 0.0f;
        dest[4] = 0.0f;
    }

    std::array<std::array<Filter, kNumBands>, kNumAudioChannels> filters;
    std::array<Coefs::Ptr, kNumBands> coefficients;

    double lastSampleRate = 44100.0;

    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>, kNumBellBands> smoothedBellFreq{
        {juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>(500.0f),
         juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative>(3000.0f)}};
    std::array<juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear>, kNumBellBands> smoothedBellGain{};
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedOutputGain{1.0f};

    std::array<BandParams, kNumBands> bands{};
    juce::AudioParameterFloat* outputGainParam = nullptr;

    // Written from the audio thread each block, read by EQCurveComponent on the message thread.
    // Only the two CV-modulated bells need this; every other displayed value comes straight from
    // its parameter, which is what lets the curve track an edit with no audio running.
    std::array<std::atomic<float>, kNumBellBands> bellCVFreq{};
    std::array<std::atomic<float>, kNumBellBands> bellCVGain{};
    std::array<std::atomic<bool>, kNumBellBands> bellCVActive{};
};
