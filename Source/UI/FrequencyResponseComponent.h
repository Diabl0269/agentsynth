#pragma once

#include "../Modules/FilterModule.h"
#include "../Modules/VisualBuffer.h"
#include "Theme/GravisynthLookAndFeel.h"
#include "Theme/Theme.h"
#include <cmath>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

class FrequencyResponseComponent
    : public juce::Component
    , public juce::Timer {
public:
    FrequencyResponseComponent(FilterModule& filter)
        : filterModule(filter) {
        magnitudes.resize(numPoints, 0.0f);
        fftData.resize(fftSize * 2, 0.0f);
        spectrumMagnitudes.resize(numPoints, -80.0f);
        startTimerHz(30);
    }

    ~FrequencyResponseComponent() override { stopTimer(); }

    void setShowSpectrum(bool show) {
        showSpectrum = show;
        repaint();
    }
    bool getShowSpectrum() const { return showSpectrum; }

    // ---------- pure static helpers (unit-testable without constructing the GUI) ----------

    // Returns the index of the maximum value in mags[0..numBins-1].
    // Returns -1 if numBins <= 0 or mags is nullptr.
    static int findPeakBin(const float* mags, int numBins) noexcept {
        if (mags == nullptr || numBins <= 0)
            return -1;
        int peak = 0;
        for (int i = 1; i < numBins; ++i)
            if (mags[i] > mags[peak])
                peak = i;
        return peak;
    }

    // Human-readable frequency label.
    // 100 -> "100Hz", 1000 -> "1kHz", 10000 -> "10kHz".
    // General rule: values >= 1000 are shown as "<N>kHz" (1 decimal if not integer);
    // values < 1000 are shown as "<N>Hz" (integer, no decimal).
    static juce::String formatHzLabel(float hz) {
        if (hz >= 1000.0f) {
            float kHz = hz / 1000.0f;
            // Suppress the decimal when it's a whole number
            if (std::fmod(kHz, 1.0f) < 0.05f)
                return juce::String((int)std::round(kHz)) + "kHz";
            return juce::String(kHz, 1) + "kHz";
        }
        return juce::String((int)std::round(hz)) + "Hz";
    }

    // Frequency (Hz) → x pixel in a log-scaled view of width `width`.
    // Mirrors freqToX() for use in unit tests (same constants: minFreq=20, maxFreq=20000).
    static float freqToXStatic(float freq, float width) noexcept {
        float t = std::log(freq / minFreq) / std::log(maxFreq / minFreq);
        return t * width;
    }

    // dB → y pixel in a view of height `height`.
    // Mirrors dbToY() for use in unit tests (same constants: minDb=-40, maxDb=50).
    static float dbToYStatic(float db, float height) noexcept {
        float normalized = (db - maxDb) / (minDb - maxDb);
        return normalized * height;
    }

    static constexpr float minFreq = 20.0f;
    static constexpr float maxFreq = 20000.0f;
    static constexpr float minDb = -40.0f;
    static constexpr float maxDb = 50.0f;

    void timerCallback() override {
        float cutoff = filterModule.getCurrentCutoff();
        float resonance = filterModule.getCurrentResonance();
        float drive = filterModule.getCurrentDrive();
        int filterType = filterModule.getCurrentFilterType();

        if (cutoff != lastCutoff || resonance != lastResonance || drive != lastDrive || filterType != lastFilterType) {
            lastCutoff = cutoff;
            lastResonance = resonance;
            lastDrive = drive;
            lastFilterType = filterType;
            recomputeMagnitudes();
            repaint();
        }

        // Update spectrum from audio data
        if (showSpectrum && filterModule.getVisualBuffer()) {
            auto* vb = filterModule.getVisualBuffer();
            std::vector<float> samples(vb->getSize(), 0.0f);
            vb->copyTo(samples);

            // Fill FFT buffer (zero-pad if needed)
            std::fill(fftData.begin(), fftData.end(), 0.0f);
            int copySize = std::min((int)samples.size(), fftSize);
            for (int i = 0; i < copySize; ++i)
                fftData[i] = samples[i];

            // Apply window and perform FFT
            window.multiplyWithWindowingTable(fftData.data(), fftSize);
            fft.performFrequencyOnlyForwardTransform(fftData.data());

            // Convert to dB at log-spaced frequency points
            double sampleRate = filterModule.getLastSampleRate();
            float binWidth = static_cast<float>(sampleRate) / static_cast<float>(fftSize);
            float normFactor = 2.0f / static_cast<float>(fftSize);

            for (int i = 0; i < numPoints; ++i) {
                float freq = indexToFreq(i);
                float exactBin = freq / binWidth;
                int bin0 = static_cast<int>(exactBin);
                int bin1 = bin0 + 1;
                float frac = exactBin - static_cast<float>(bin0);

                float mag = 0.0f;
                if (bin0 >= 0 && bin1 < fftSize / 2) {
                    mag = fftData[bin0] * (1.0f - frac) + fftData[bin1] * frac;
                } else if (bin0 >= 0 && bin0 < fftSize / 2) {
                    mag = fftData[bin0];
                }
                mag *= normFactor;

                float db = 20.0f * std::log10(std::max(mag, 0.00001f));
                float target = juce::jlimit(-80.0f, 0.0f, db);
                // Smooth the spectrum (exponential moving average)
                spectrumMagnitudes[i] += 0.3f * (target - spectrumMagnitudes[i]);
            }

            repaint();
        }
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat();
        float w = bounds.getWidth();
        float h = bounds.getHeight();

        using synth::theme::GravisynthLookAndFeel;
        auto* lf = dynamic_cast<GravisynthLookAndFeel*>(&getLookAndFeel());

        juce::Colour bgColor = lf ? lf->getTheme().colors.bg1 : juce::Colour(0xff1a1a2e);
        juce::Colour gridColor = lf ? lf->getTheme().colors.border.withAlpha(0.4f) : juce::Colour(0xff2a2a3e);
        juce::Colour mutedText = lf ? lf->getTheme().colors.textMuted : juce::Colour(0xff6a6a7e);

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

        // Build the path from magnitude data
        juce::Path curvePath;
        juce::Path fillPath;

        bool first = true;
        for (int i = 0; i < numPoints; ++i) {
            float freq = indexToFreq(i);
            float x = freqToX(freq, w);
            float y = dbToY(magnitudes[i], h);
            y = juce::jlimit(0.0f, h, y);

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
        juce::Colour accent = lf ? lf->getTheme().colors.accent : juce::Colour(0xff00b4d8);
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
            constexpr float specMinDb = -80.0f;
            constexpr float specMaxDb = 0.0f;

            juce::Path specPath;
            juce::Path specFill;
            bool specFirst = true;
            for (int i = 0; i < numPoints; ++i) {
                float freq = indexToFreq(i);
                float x = freqToX(freq, w);
                // Map spectrum dB to full component height
                float normY = (spectrumMagnitudes[i] - specMaxDb) / (specMinDb - specMaxDb);
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

            juce::Colour accent2 = lf ? lf->getTheme().colors.accent2 : juce::Colour(0xff00D1FF);

            // Semi-transparent green fill
            juce::ColourGradient specGradient(accent2.withAlpha(0.188f), 0.0f, 0.0f, accent2.withAlpha(0.031f), 0.0f, h,
                                              false); // 0x30=48/255≈0.188, 0x08=8/255≈0.031
            g.setGradientFill(specGradient);
            g.fillPath(specFill);

            g.setColour(accent2.withAlpha(0.67f)); // 0xaa == 170/255 ≈ 0.67
            g.strokePath(specPath, juce::PathStrokeType(1.0f));
        }

        // Cutoff frequency marker
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

    // Spectrum analyzer
    bool showSpectrum = false;
    static constexpr int fftOrder = 10; // 1024-point FFT
    static constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData;
    std::vector<float> spectrumMagnitudes;

    float indexToFreq(int i) const {
        float t = static_cast<float>(i) / static_cast<float>(numPoints - 1);
        return minFreq * std::pow(maxFreq / minFreq, t);
    }

    float freqToX(float freq, float width) const {
        float t = std::log(freq / minFreq) / std::log(maxFreq / minFreq);
        return t * width;
    }

    float dbToY(float db, float height) const {
        float normalized = (db - maxDb) / (minDb - maxDb); // 0 at top (maxDb), 1 at bottom (minDb)
        return normalized * height;
    }

    void recomputeMagnitudes() {
        float cutoff = lastCutoff;
        float resonance = lastResonance;
        int filterType = lastFilterType;

        if (cutoff < 20.0f)
            cutoff = 20.0f;

        float Q = 0.707f + resonance * 15.0f;

        for (int i = 0; i < numPoints; ++i) {
            float freq = indexToFreq(i);
            float mag = computeMagnitude(freq, cutoff, Q, filterType);
            float db = 20.0f * std::log10(std::max(mag, 0.0001f));
            magnitudes[i] = juce::jlimit(minDb, maxDb, db);
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
