#pragma once

#include "FilterSpectrumAnalyser.h"
#include "FrequencyGrid.h"
#include "Modules/Filter/CustomFilterBlend.h"
#include "Modules/FilterModule.h"
#include "Modules/VisualBuffer.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

class FrequencyResponseComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer {
public:
    FrequencyResponseComponent(FilterModule& filter)
        : filterModule(filter) {
        magnitudes.resize(numPoints, 0.0f);
        setTitle("Filter response");
        setDescription("Frequency response curve of the filter");
        setTooltip("Filter response: the filter's gain across 20 Hz to 20 kHz");
        // Timer starts only while showing — the Filter card hides this view by default, and a
        // free-running 30 Hz tick on every Filter instance would be wasted work (and a repaint
        // source) for a component the user has not opened.
    }

    ~FrequencyResponseComponent() override { stopTimer(); }

    void visibilityChanged() override {
        if (isVisible())
            startTimerHz(30);
        else
            stopTimer();
    }

    void setShowSpectrum(bool show) {
        showSpectrum = show;
        repaint();
    }
    bool getShowSpectrum() const { return showSpectrum; }

    // Caps peaks at maxDb but leaves deep roll-off unclamped so paint can send the path below
    // the view (no horizontal floor stroke along the bottom edge).
    static float plotDb(float db) noexcept { return juce::jmin(maxDb, db); }

    // ---------- pure static helpers (unit-testable without constructing the GUI) ----------
    // The axis maths lives in synth::ui::FrequencyGrid so this view and EQCurveComponent
    // (Parametric EQ) plot on identical axes. These stay as the component's public API.

    // Returns the index of the maximum value in mags[0..numBins-1].
    // Returns -1 if numBins <= 0 or mags is nullptr.
    static int findPeakBin(const float* mags, int numBins) noexcept {
        return synth::ui::FrequencyGrid::findPeakBin(mags, numBins);
    }

    // Human-readable frequency label.
    // 100 -> "100Hz", 1000 -> "1kHz", 10000 -> "10kHz".
    // General rule: values >= 1000 are shown as "<N>kHz" (1 decimal if not integer);
    // values < 1000 are shown as "<N>Hz" (integer, no decimal).
    static juce::String formatHzLabel(float hz) { return synth::ui::FrequencyGrid::formatHzLabel(hz); }

    // Frequency (Hz) → x pixel in a log-scaled view of width `width`.
    // Mirrors freqToX() for use in unit tests (same constants: minFreq=20, maxFreq=20000).
    static float freqToXStatic(float freq, float width) noexcept {
        return synth::ui::FrequencyGrid::freqToX(freq, width);
    }

    // dB → y pixel in a view of height `height`.
    // Mirrors dbToY() for use in unit tests (same constants: minDb=-40, maxDb=50).
    static float dbToYStatic(float db, float height) noexcept {
        return synth::ui::FrequencyGrid::dbToY(db, height, minDb, maxDb);
    }

    static constexpr float minFreq = synth::ui::FrequencyGrid::kMinFreq;
    static constexpr float maxFreq = synth::ui::FrequencyGrid::kMaxFreq;
    // Asymmetric on purpose: a resonant ladder peak overshoots far above unity gain.
    static constexpr float minDb = -40.0f;
    static constexpr float maxDb = 50.0f;

    void timerCallback() override {
        float cutoff = filterModule.getCurrentCutoff();
        float resonance = filterModule.getCurrentResonance();
        float drive = filterModule.getCurrentDrive();
        int filterType = filterModule.getCurrentFilterType();

        const bool custom = filterType == FilterModule::kCustomTypeIndex;
        const CustomState customNow{filterModule.getCurrentMorphX(),
                                    filterModule.getCurrentMorphY(),
                                    filterModule.getCurrentShift(),
                                    filterModule.getCurrentDepth(),
                                    cutoff,
                                    filterModule.getModulatedResonance(),
                                    filterModule.getCustomShapesGeneration()};
        if (cutoff != lastCutoff || resonance != lastResonance || drive != lastDrive || filterType != lastFilterType ||
            (custom && !(customNow == lastCustom))) {
            lastCutoff = cutoff;
            lastResonance = resonance;
            lastDrive = drive;
            lastFilterType = filterType;
            lastCustom = customNow;
            recomputeMagnitudes();
            repaint();
        }

        // Update spectrum from audio data
        if (showSpectrum && filterModule.getVisualBuffer()) {
            spectrum.analyse(*filterModule.getVisualBuffer(), filterModule.getLastSampleRate());
            repaint();
        }
    }

    /** Custom type only: a double-click on the view (the card opens the shape editor on the nearest shape). */
    std::function<void()> onCustomDoubleClick;
    void mouseDoubleClick(const juce::MouseEvent&) override {
        if (lastFilterType == FilterModule::kCustomTypeIndex && onCustomDoubleClick)
            onCustomDoubleClick();
    }
    /** The smoothed spectrum points (1024 values in dB), for tests. */
    const std::vector<float>& getSpectrumDbForTest() const { return spectrum.getDb(); }
    /** The plotted dB at curve point `index` (of 1024), for tests. */
    float getMagnitudeDbForTest(int index) const { return magnitudes[(size_t)index]; }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();

        juce::Colour bgColor = synth::theme::themeOf(*this).colors.bg1;
        juce::Colour gridColor = synth::theme::themeOf(*this).colors.border.withAlpha(0.4f);
        juce::Colour mutedText = synth::theme::themeOf(*this).colors.textMuted;

        // Dark background
        g.fillAll(bgColor);

        // Grid lines at 100Hz, 1kHz, 10kHz
        g.setColour(gridColor);
        for (float freq : {100.0f, 1000.0f, 10000.0f}) {
            float x = freqToX(freq, w);
            g.drawVerticalLine((int)x, 0.0f, h);
        }
        // dB horizontal grid lines at -20, 0, +20
        for (float db : {-20.0f, 0.0f, 20.0f}) {
            float y = dbToY(db, h);
            g.drawHorizontalLine((int)y, 0.0f, w);
        }
        // Hz axis labels at vertical grid lines (top of each gridline, 3 px offset right)
        {
            g.setFont(juce::Font(9.5f));
            g.setColour(mutedText); // muted label colour
            const struct {
                float freq;
                const char* label;
            } hzLabels[] = {{100.0f, "100Hz"}, {1000.0f, "1kHz"}, {10000.0f, "10kHz"}};
            for (const auto& lbl : hzLabels) {
                float x = freqToX(lbl.freq, w);
                // 10kHz is the rightmost label — right-justify it so it stays inside the
                // component bounds at narrow widths; interior labels remain left-anchored.
                if (lbl.freq >= 10000.0f)
                    g.drawText(lbl.label, (int)x - 45, 2, 48, 12, juce::Justification::right, false);
                else
                    g.drawText(lbl.label, (int)x + 3, 2, 48, 12, juce::Justification::left, false);
            }
        }

        // dB axis labels at horizontal grid lines (left edge, vertically centred on line)
        {
            g.setFont(juce::Font(9.0f));
            g.setColour(mutedText);
            const struct {
                float db;
                const char* label;
            } dbLabels[] = {{20.0f, "+20"}, {0.0f, "0"}, {-20.0f, "-20"}};
            for (const auto& lbl : dbLabels) {
                float y = dbToY(lbl.db, h);
                g.drawText(lbl.label, 3, (int)y - 6, 28, 12, juce::Justification::left, false);
            }
        }

        // Build the path from magnitude data. Y is intentionally unclamped: once the response
        // falls past minDb the path continues below the component and is clipped by the paint
        // clip region. Clamping to h used to leave a flat "floor" stroke across the right side
        // of a resonant low-pass (and any other steep roll-off).
        juce::Path curvePath;
        juce::Path fillPath;

        bool first = true;
        for (int i = 0; i < numPoints; ++i) {
            float freq = indexToFreq(i);
            float x = freqToX(freq, w);
            float y = dbToY(magnitudes[i], h);

            if (first) {
                curvePath.startNewSubPath(x, y);
                fillPath.startNewSubPath(x, h); // bottom
                fillPath.lineTo(x, y);
                first = false;
            } else {
                curvePath.lineTo(x, y);
                fillPath.lineTo(x, y);
            }
        }

        // Close fill path
        fillPath.lineTo(w, h);
        fillPath.closeSubPath();

        // Gradient fill under curve
        juce::Colour accent = synth::theme::themeOf(*this).colors.accent;
        juce::ColourGradient gradient(accent.withAlpha(0.375f), 0.0f, 0.0f, // 0x60 == 96/255 ≈ 0.375
                                      accent.withAlpha(0.063f), 0.0f, h,    // 0x10 == 16/255 ≈ 0.063
                                      false);
        g.setGradientFill(gradient);
        g.fillPath(fillPath);

        // Stroke the curve
        g.setColour(accent); // bright cyan
        g.strokePath(curvePath, juce::PathStrokeType(2.0f));

        // Resonance peak marker — filled dot + small callout at the magnitude peak
        {
            int peakBin = findPeakBin(magnitudes.data(), (int)magnitudes.size());
            if (peakBin >= 0) {
                float peakFreq = indexToFreq(peakBin);
                float peakX = freqToX(peakFreq, w);
                float peakY = juce::jlimit(0.0f, h, dbToY(magnitudes[peakBin], h));
                constexpr float dotRadius = 3.5f;
                // Filled dot in accent cyan with a dark outline for contrast
                g.setColour(bgColor);
                g.fillEllipse(peakX - dotRadius - 1.0f, peakY - dotRadius - 1.0f, (dotRadius + 1.0f) * 2.0f,
                              (dotRadius + 1.0f) * 2.0f);
                g.setColour(accent); // same accent as curve
                g.fillEllipse(peakX - dotRadius, peakY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
                // Small text callout (peak frequency label), placed above the dot.
                // Skip the label entirely when the component is too narrow for the 44 px rect
                // (jlimit upper bound would invert if w < 44).
                if (w >= 44.0f) {
                    juce::String callout = formatHzLabel(peakFreq);
                    g.setFont(juce::Font(8.5f));
                    g.setColour(accent.withAlpha(0.8f)); // 0xcc == 204/255 ≈ 0.8
                    int labelX = juce::jlimit(0, (int)w - 44, (int)peakX - 20);
                    int labelY = juce::jlimit(0, (int)h - 12, (int)peakY - 14);
                    g.drawText(callout, labelX, labelY, 44, 10, juce::Justification::centred, false);
                }
            }
        }

        // Draw live spectrum (green) — uses its own dB range for visibility
        if (showSpectrum) {
            constexpr float specMinDb = synth::ui::FilterSpectrumAnalyser::kMinDb;
            constexpr float specMaxDb = synth::ui::FilterSpectrumAnalyser::kMaxDb;

            juce::Path specPath;
            juce::Path specFill;
            bool specFirst = true;
            for (int i = 0; i < numPoints; ++i) {
                float freq = indexToFreq(i);
                float x = freqToX(freq, w);
                // Map spectrum dB to full component height
                float normY = (spectrum.getDb()[(size_t)i] - specMaxDb) / (specMinDb - specMaxDb);
                float y = juce::jlimit(0.0f, h, normY * h);

                if (specFirst) {
                    specPath.startNewSubPath(x, y);
                    specFill.startNewSubPath(x, h);
                    specFill.lineTo(x, y);
                    specFirst = false;
                } else {
                    specPath.lineTo(x, y);
                    specFill.lineTo(x, y);
                }
            }
            specFill.lineTo(w, h);
            specFill.closeSubPath();

            juce::Colour accent2 = synth::theme::themeOf(*this).colors.accent2;

            // Semi-transparent green fill
            juce::ColourGradient specGradient(accent2.withAlpha(0.188f), 0.0f, 0.0f, accent2.withAlpha(0.031f), 0.0f, h,
                                              false); // 0x30=48/255≈0.188, 0x08=8/255≈0.031
            g.setGradientFill(specGradient);
            g.fillPath(specFill);

            g.setColour(accent2.withAlpha(0.67f)); // 0xaa == 170/255 ≈ 0.67
            g.strokePath(specPath, juce::PathStrokeType(1.0f));
        }

        // Cutoff frequency marker (in Custom, where the resonant bell sits)
        float cutoffX = freqToX(lastCutoff, w);
        g.setColour(accent.withAlpha(0.25f)); // 0x40 == 64/255 ≈ 0.25
        g.drawVerticalLine((int)cutoffX, 0.0f, h);
    }

private:
    FilterModule& filterModule;
    std::vector<float> magnitudes;
    static constexpr int numPoints = 1024;

    float lastCutoff = 0.0f;
    float lastResonance = 0.0f;
    float lastDrive = 0.0f;
    int lastFilterType = -1;
    struct CustomState {
        float morphX = -1.0f, morphY = -1.0f, shift = 0.0f, depth = 0.0f, cutoff = 0.0f, resonance = 0.0f;
        int generation = -1;
        bool operator==(const CustomState&) const = default;
    } lastCustom;

    // Spectrum analyzer
    bool showSpectrum = false;
    synth::ui::FilterSpectrumAnalyser spectrum;

    float indexToFreq(int i) const { return synth::ui::FrequencyGrid::indexToFreq(i, numPoints); }

    float freqToX(float freq, float width) const { return synth::ui::FrequencyGrid::freqToX(freq, width); }

    // 0 at top (maxDb), height at bottom (minDb)
    float dbToY(float db, float height) const { return synth::ui::FrequencyGrid::dbToY(db, height, minDb, maxDb); }

    void recomputeMagnitudes() {
        float cutoff = lastCutoff;
        float resonance = lastResonance;
        int filterType = lastFilterType;

        if (filterType == FilterModule::kCustomTypeIndex) {
            // The drawn shapes blended at the morph cursor, shifted (Shift and Cutoff), scaled, then given the
            // resonant bell: what the kernel is designed from.
            const auto db = synth::customfilter::responseGridDb(filterModule.getCustomShapes(), lastCustom.morphX,
                                                                lastCustom.morphY, lastCustom.shift, lastCustom.depth,
                                                                lastCustom.cutoff, lastCustom.resonance);
            for (int i = 0; i < numPoints; ++i)
                magnitudes[(size_t)i] = plotDb(db[(size_t)i]);
            return;
        }

        if (cutoff < 20.0f)
            cutoff = 20.0f;

        float Q = 0.707f + resonance * 15.0f;

        for (int i = 0; i < numPoints; ++i) {
            float freq = indexToFreq(i);
            float mag = computeMagnitude(freq, cutoff, Q, filterType);
            float db = 20.0f * std::log10(std::max(mag, 0.0001f));
            magnitudes[i] = plotDb(db);
        }
    }

    float computeMagnitude(float freq, float cutoff, float Q, int filterType) const {
        float w = freq / cutoff;
        float w2 = w * w;
        float invQ = 1.0f / Q;
        float denom2 = (1.0f - w2) * (1.0f - w2) + w2 * invQ * invQ;
        float denom = std::sqrt(denom2);

        switch (filterType) {
        case 0: // LPF24
        {
            float lpf12 = 1.0f / denom;
            return lpf12 * lpf12;
        }
        case 1: // LPF12
            return 1.0f / denom;
        case 2: // HPF24
        {
            float hpf12 = w2 / denom;
            return hpf12 * hpf12;
        }
        case 3: // HPF12
            return w2 / denom;
        case 4: // BPF24
        {
            float bpf12 = (w * invQ) / denom;
            return bpf12 * bpf12;
        }
        case 5: // BPF12
            return (w * invQ) / denom;
        case 6: // Notch
            return std::abs(1.0f - w2) / denom;
        default:
            return 1.0f;
        }
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FrequencyResponseComponent)
};
