#pragma once

#include "../Modules/FX/ParametricEQModule.h"
#include "../Modules/VisualBuffer.h"
#include "FrequencyGrid.h"
#include "Theme/AppLookAndFeel.h"
#include "Theme/Theme.h"
#include <algorithm>
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Visual response curve for ParametricEQModule.
 *
 *  Shares its log-frequency / dB mapping with FrequencyResponseComponent via
 *  synth::ui::FrequencyGrid, and gets the curve itself from the module's own analytic
 *  prototypes (ParametricEQModule::responseDb), so what is drawn is the response the biquads
 *  actually realise rather than a separate approximation.
 *
 *  Repaint discipline (see CLAUDE.md "No unconditional per-tick repaint"): the 30 Hz timer only
 *  repaints when a band setting has changed, or continuously while the spectrum overlay is on
 *  (which needs a fresh FFT each frame by definition).
 */
class EQCurveComponent
    : public juce::Component
    , public juce::Timer {
public:
    /** Symmetric dB window — an EQ cuts as much as it boosts, so 0 dB sits dead centre. */
    static constexpr float minDb = -30.0f;
    static constexpr float maxDb = 30.0f;

    explicit EQCurveComponent(ParametricEQModule& eq)
        : eqModule(eq) {
        magnitudes.resize(numPoints, 0.0f);
        fftData.resize(fftSize * 2, 0.0f);
        spectrumMagnitudes.resize(numPoints, -80.0f);
        lastBands = eqModule.getBandSnapshots();
        lastOutputGainDb = eqModule.getOutputGainDb();
        recomputeMagnitudes();
        startTimerHz(30);
    }

    ~EQCurveComponent() override { stopTimer(); }

    void setShowSpectrum(bool show) {
        showSpectrum = show;
        repaint();
    }
    bool getShowSpectrum() const { return showSpectrum; }

    // ---------- pure static helpers (unit-testable without constructing the GUI) ----------

    /** Frequency (Hz) → x pixel. Thin alias over FrequencyGrid for tests and callers. */
    static float freqToXStatic(float freq, float width) noexcept {
        return synth::ui::FrequencyGrid::freqToX(freq, width);
    }

    /** dB → y pixel using this component's symmetric ±30 dB window. */
    static float dbToYStatic(float db, float height) noexcept {
        return synth::ui::FrequencyGrid::dbToY(db, height, minDb, maxDb);
    }

    void timerCallback() override {
        const auto bands = eqModule.getBandSnapshots();
        const float outputGainDb = eqModule.getOutputGainDb();

        if (bandsDiffer(bands, lastBands) || outputGainDb != lastOutputGainDb) {
            lastBands = bands;
            lastOutputGainDb = outputGainDb;
            recomputeMagnitudes();
            repaint();
        }

        if (showSpectrum && eqModule.getVisualBuffer() != nullptr) {
            updateSpectrum();
            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        using synth::ui::FrequencyGrid;

        auto bounds = getLocalBounds().toFloat();
        const float w = bounds.getWidth();
        const float h = bounds.getHeight();
        if (w <= 0.0f || h <= 0.0f)
            return;

        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour bgColor = lf ? lf->getTheme().colors.bg1 : juce::Colour(0xff1a1a2e);
        const juce::Colour gridColor = lf ? lf->getTheme().colors.border.withAlpha(0.4f) : juce::Colour(0xff2a2a3e);
        const juce::Colour mutedText = lf ? lf->getTheme().colors.textMuted : juce::Colour(0xff6a6a7e);
        const juce::Colour accent = lf ? lf->getTheme().colors.accent : juce::Colour(0xff00b4d8);
        const juce::Colour accent2 = lf ? lf->getTheme().colors.accent2 : juce::Colour(0xff00D1FF);

        g.fillAll(bgColor);

        // ---- Grid: log-frequency verticals, dB horizontals ----
        g.setColour(gridColor);
        for (float freq : {100.0f, 1000.0f, 10000.0f})
            g.drawVerticalLine((int)FrequencyGrid::freqToX(freq, w), 0.0f, h);
        for (float db : {-24.0f, -12.0f, 12.0f, 24.0f})
            g.drawHorizontalLine((int)dbToYStatic(db, h), 0.0f, w);

        // The 0 dB line is the reference the whole curve is read against — draw it brighter.
        const float zeroY = dbToYStatic(0.0f, h);
        g.setColour(mutedText.withAlpha(0.55f));
        g.drawHorizontalLine((int)zeroY, 0.0f, w);

        // Hz axis labels (top of each vertical gridline; the rightmost is right-justified so it
        // stays inside the component at narrow widths).
        g.setFont(juce::Font(9.5f));
        g.setColour(mutedText);
        const struct {
            float freq;
            const char* label;
        } hzLabels[] = {{100.0f, "100Hz"}, {1000.0f, "1kHz"}, {10000.0f, "10kHz"}};
        for (const auto& lbl : hzLabels) {
            const float x = FrequencyGrid::freqToX(lbl.freq, w);
            if (lbl.freq >= 10000.0f)
                g.drawText(lbl.label, (int)x - 45, 2, 48, 12, juce::Justification::right, false);
            else
                g.drawText(lbl.label, (int)x + 3, 2, 48, 12, juce::Justification::left, false);
        }

        // dB axis labels
        g.setFont(juce::Font(9.0f));
        const struct {
            float db;
            const char* label;
        } dbLabels[] = {{24.0f, "+24"}, {12.0f, "+12"}, {0.0f, "0"}, {-12.0f, "-12"}, {-24.0f, "-24"}};
        for (const auto& lbl : dbLabels)
            g.drawText(lbl.label, 3, (int)dbToYStatic(lbl.db, h) - 6, 28, 12, juce::Justification::left, false);

        // ---- Live spectrum underlay (drawn first so the curve stays legible on top) ----
        if (showSpectrum)
            paintSpectrum(g, w, h, accent2);

        // ---- Response curve, with the fill hanging off the 0 dB line ----
        juce::Path curvePath;
        juce::Path fillPath;
        const float clampedZeroY = juce::jlimit(0.0f, h, zeroY);
        bool first = true;
        for (int i = 0; i < numPoints; ++i) {
            const float x = FrequencyGrid::freqToX(FrequencyGrid::indexToFreq(i, numPoints), w);
            const float y = juce::jlimit(0.0f, h, dbToYStatic(magnitudes[(size_t)i], h));
            if (first) {
                curvePath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, clampedZeroY);
                fillPath.lineTo(x, y);
                first = false;
            } else {
                curvePath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }
        fillPath.lineTo(w, clampedZeroY);
        fillPath.closeSubPath();

        g.setColour(accent.withAlpha(0.22f));
        g.fillPath(fillPath);
        g.setColour(accent);
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));

        // ---- Band handles: one dot per band at (centre freq, band gain) ----
        constexpr float dotRadius = 4.0f;
        for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
            const auto& band = lastBands[(size_t)b];
            const float bx = FrequencyGrid::freqToX(band.freqHz, w);
            const float by = juce::jlimit(0.0f, h, dbToYStatic(band.gainDb, h));

            // Dark ring first so the dot reads against both the curve and the fill.
            g.setColour(bgColor);
            g.fillEllipse(bx - dotRadius - 1.0f, by - dotRadius - 1.0f, (dotRadius + 1.0f) * 2.0f,
                          (dotRadius + 1.0f) * 2.0f);
            // Flat bands are drawn hollow so the eye goes straight to the ones doing work.
            const bool active = std::abs(band.gainDb) > 0.25f;
            g.setColour(active ? accent : accent.withAlpha(0.45f));
            if (active)
                g.fillEllipse(bx - dotRadius, by - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
            else
                g.drawEllipse(bx - dotRadius, by - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f, 1.2f);

            if (active && w >= 44.0f) {
                g.setFont(juce::Font(8.5f));
                g.setColour(accent.withAlpha(0.8f));
                const int labelX = juce::jlimit(0, (int)w - 44, (int)bx - 22);
                // Keep the callout on the far side of the dot from the curve's excursion.
                const int labelY = juce::jlimit(0, (int)h - 12, (int)by + (band.gainDb >= 0.0f ? -15 : 5));
                g.drawText(FrequencyGrid::formatHzLabel(band.freqHz), labelX, labelY, 44, 10,
                           juce::Justification::centred, false);
            }
        }
    }

private:
    static constexpr int numPoints = 512;
    static constexpr int fftOrder = 10; // 1024-point FFT
    static constexpr int fftSize = 1 << fftOrder;

    static bool bandsDiffer(const std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>& a,
                            const std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>& b) {
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].freqHz != b[i].freqHz || a[i].gainDb != b[i].gainDb || a[i].q != b[i].q)
                return true;
        return false;
    }

    void recomputeMagnitudes() {
        for (int i = 0; i < numPoints; ++i) {
            const float freq = synth::ui::FrequencyGrid::indexToFreq(i, numPoints);
            magnitudes[(size_t)i] =
                juce::jlimit(minDb, maxDb, ParametricEQModule::responseDb(lastBands, lastOutputGainDb, freq));
        }
    }

    void updateSpectrum() {
        auto* vb = eqModule.getVisualBuffer();
        if (vb == nullptr)
            return;

        std::vector<float> samples(vb->getSize(), 0.0f);
        vb->copyTo(samples);

        std::fill(fftData.begin(), fftData.end(), 0.0f);
        const int copySize = std::min((int)samples.size(), fftSize);
        for (int i = 0; i < copySize; ++i)
            fftData[(size_t)i] = samples[(size_t)i];

        window.multiplyWithWindowingTable(fftData.data(), fftSize);
        fft.performFrequencyOnlyForwardTransform(fftData.data());

        const float binWidth = static_cast<float>(eqModule.getLastSampleRate()) / static_cast<float>(fftSize);
        const float normFactor = 2.0f / static_cast<float>(fftSize);

        for (int i = 0; i < numPoints; ++i) {
            const float freq = synth::ui::FrequencyGrid::indexToFreq(i, numPoints);
            const float exactBin = freq / binWidth;
            const int bin0 = static_cast<int>(exactBin);
            const int bin1 = bin0 + 1;
            const float frac = exactBin - static_cast<float>(bin0);

            float mag = 0.0f;
            if (bin0 >= 0 && bin1 < fftSize / 2)
                mag = fftData[(size_t)bin0] * (1.0f - frac) + fftData[(size_t)bin1] * frac;
            else if (bin0 >= 0 && bin0 < fftSize / 2)
                mag = fftData[(size_t)bin0];
            mag *= normFactor;

            const float db = 20.0f * std::log10(std::max(mag, 0.00001f));
            const float target = juce::jlimit(kSpecMinDb, kSpecMaxDb, db);
            // Exponential moving average so the display settles instead of flickering.
            spectrumMagnitudes[(size_t)i] += 0.3f * (target - spectrumMagnitudes[(size_t)i]);
        }
    }

    void paintSpectrum(juce::Graphics& g, float w, float h, juce::Colour colour) {
        juce::Path specPath;
        juce::Path specFill;
        bool first = true;
        for (int i = 0; i < numPoints; ++i) {
            const float x = synth::ui::FrequencyGrid::freqToX(synth::ui::FrequencyGrid::indexToFreq(i, numPoints), w);
            // The spectrum has its own -80..0 dB window, mapped over the full height.
            const float normY = (spectrumMagnitudes[(size_t)i] - kSpecMaxDb) / (kSpecMinDb - kSpecMaxDb);
            const float y = juce::jlimit(0.0f, h, normY * h);
            if (first) {
                specPath.startNewSubPath(x, y);
                specFill.startNewSubPath(x, h);
                specFill.lineTo(x, y);
                first = false;
            } else {
                specPath.lineTo(x, y);
                specFill.lineTo(x, y);
            }
        }
        specFill.lineTo(w, h);
        specFill.closeSubPath();

        juce::ColourGradient specGradient(colour.withAlpha(0.188f), 0.0f, 0.0f, colour.withAlpha(0.031f), 0.0f, h,
                                          false);
        g.setGradientFill(specGradient);
        g.fillPath(specFill);
        g.setColour(colour.withAlpha(0.55f));
        g.strokePath(specPath, juce::PathStrokeType(1.0f));
    }

    static constexpr float kSpecMinDb = -80.0f;
    static constexpr float kSpecMaxDb = 0.0f;

    ParametricEQModule& eqModule;

    std::vector<float> magnitudes;
    std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands> lastBands{};
    float lastOutputGainDb = 0.0f;

    bool showSpectrum = false;
    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData;
    std::vector<float> spectrumMagnitudes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveComponent)
};
