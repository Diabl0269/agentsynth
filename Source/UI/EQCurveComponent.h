#pragma once

#include "../Modules/FX/ParametricEQModule.h"
#include "../Modules/VisualBuffer.h"
#include "FrequencyGrid.h"
#include "Theme/AppLookAndFeel.h"
#include "Theme/Theme.h"
#include <algorithm>
#include <cmath>
#include <functional>
#include <juce_dsp/juce_dsp.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Interactive response curve for ParametricEQModule, in the traditional DAW idiom.
 *
 *  Gestures (all four bands start disabled, so the curve starts empty):
 *    - double-click empty space  → add a point, enabling the slot that best fits that frequency
 *    - double-click a point      → remove it (disables that band)
 *    - drag a point              → set its frequency (x) and gain (y)
 *    - scroll over a point       → widen / narrow it (Q)
 *
 *  The mouse handlers are deliberately thin wrappers over addPointAt / removeBand / dragBandTo /
 *  nudgeBandQ, which are public so the interaction can be unit-tested without synthesising
 *  juce::MouseEvents.
 *
 *  Curve maths comes from the module's own analytic prototypes (ParametricEQModule::responseDb),
 *  so what is drawn is the response the biquads actually realise rather than a separate
 *  approximation. Axis maths is shared with FrequencyResponseComponent via synth::ui::FrequencyGrid.
 *
 *  Repaint discipline (see CLAUDE.md "No unconditional per-tick repaint"): the 30 Hz timer
 *  repaints when a band setting changed, when the hover/selection changed, or while the spectrum
 *  overlay has actual signal to draw. A silent patch settles to zero repaints even with the
 *  spectrum switched on.
 */
class EQCurveComponent
    : public juce::Component
    , public juce::Timer {
public:
    /** Symmetric dB window — an EQ cuts as much as it boosts, so 0 dB sits dead centre. */
    static constexpr float minDb = -30.0f;
    static constexpr float maxDb = 30.0f;
    /** Click/hover radius around a band handle, in pixels. */
    static constexpr float kHitRadius = 11.0f;

    explicit EQCurveComponent(ParametricEQModule& eq)
        : eqModule(eq) {
        magnitudes.resize(numPoints, 0.0f);
        fftData.resize(fftSize * 2, 0.0f);
        spectrumMagnitudes.resize(numPoints, kSpecMinDb);
        lastBands = eqModule.getBandSnapshots();
        lastOutputGainDb = eqModule.getOutputGainDb();
        recomputeMagnitudes();
        setWantsKeyboardFocus(false);
        startTimerHz(30);
    }

    ~EQCurveComponent() override { stopTimer(); }

    /** Called around every parameter-changing gesture so the host can bracket it in one undo
     *  step. Both are optional; when unset the gestures still work, just without undo entries.
     */
    std::function<void()> onGestureStart;
    std::function<void()> onGestureEnd;

    void setShowSpectrum(bool show) {
        showSpectrum = show;
        repaint();
    }
    bool getShowSpectrum() const { return showSpectrum; }

    int getSelectedBand() const { return selectedBand; }
    int getHoveredBand() const { return hoveredBand; }

    // ---------- coordinate mapping ----------

    /** Frequency (Hz) → x pixel. Thin alias over FrequencyGrid for tests and callers. */
    static float freqToXStatic(float freq, float width) noexcept {
        return synth::ui::FrequencyGrid::freqToX(freq, width);
    }

    /** dB → y pixel using this component's symmetric ±30 dB window. */
    static float dbToYStatic(float db, float height) noexcept {
        return synth::ui::FrequencyGrid::dbToY(db, height, minDb, maxDb);
    }

    float freqAtX(float x) const noexcept {
        return synth::ui::FrequencyGrid::xToFreq(x, static_cast<float>(getWidth()));
    }
    float gainAtY(float y) const noexcept {
        return juce::jlimit(-ParametricEQModule::kMaxGainDb, ParametricEQModule::kMaxGainDb,
                            synth::ui::FrequencyGrid::yToDb(y, static_cast<float>(getHeight()), minDb, maxDb));
    }
    juce::Point<float> bandPosition(const ParametricEQModule::BandSnapshot& band) const noexcept {
        return {freqToXStatic(band.freqHz, static_cast<float>(getWidth())),
                dbToYStatic(band.gainDb, static_cast<float>(getHeight()))};
    }

    // ---------- interaction primitives (public so tests can drive them directly) ----------

    /** Nearest ENABLED band whose handle is within kHitRadius of `p`, or -1 if none is. */
    int hitTestBand(juce::Point<float> p) const {
        int best = -1;
        float bestDistanceSq = kHitRadius * kHitRadius;
        for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
            if (!lastBands[(size_t)b].enabled)
                continue;
            const float distanceSq = bandPosition(lastBands[(size_t)b]).getDistanceSquaredFrom(p);
            if (distanceSq <= bestDistanceSq) {
                best = b;
                bestDistanceSq = distanceSq;
            }
        }
        return best;
    }

    /** Enables the slot that best fits the clicked frequency and parks it at the clicked point.
     *  Returns the band index, or -1 when all four slots are already in use.
     */
    int addPointAt(juce::Point<float> p) {
        const float freq = freqAtX(p.x);
        const int band = eqModule.findBandForNewPoint(freq);
        if (band < 0)
            return -1;

        beginGesture();
        eqModule.setBandFreq(band, freq);
        eqModule.setBandGain(band, gainAtY(p.y));
        eqModule.setBandQ(band, ParametricEQModule::kDefaultQ);
        eqModule.setBandEnabled(band, true);
        endGesture();

        selectedBand = band;
        refreshFromModule();
        return band;
    }

    /** Removes a point — the band keeps its settings, it just stops contributing. */
    void removeBand(int band) {
        if (band < 0 || band >= ParametricEQModule::kNumBands || !eqModule.isBandEnabled(band))
            return;
        beginGesture();
        eqModule.setBandEnabled(band, false);
        endGesture();
        if (selectedBand == band)
            selectedBand = -1;
        refreshFromModule();
    }

    /** Moves a band's handle to `p` — x sets frequency, y sets gain. */
    void dragBandTo(int band, juce::Point<float> p) {
        if (band < 0 || band >= ParametricEQModule::kNumBands)
            return;
        eqModule.setBandFreq(band, freqAtX(p.x));
        eqModule.setBandGain(band, gainAtY(p.y));
        refreshFromModule();
    }

    /** Multiplicatively widens (negative) or narrows (positive) a band's Q. */
    void nudgeBandQ(int band, float amount) {
        if (band < 0 || band >= ParametricEQModule::kNumBands)
            return;
        const float current = lastBands[(size_t)band].q;
        const float updated = current * std::pow(2.0f, amount);
        beginGesture();
        eqModule.setBandQ(band, updated);
        endGesture();
        refreshFromModule();
    }

    // ---------- mouse handling ----------

    void mouseMove(const juce::MouseEvent& e) override {
        const int band = hitTestBand(e.position);
        if (band != hoveredBand) {
            hoveredBand = band;
            setMouseCursor(band >= 0 ? juce::MouseCursor::UpDownLeftRightResizeCursor
                                     : juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hoveredBand != -1) {
            hoveredBand = -1;
            setMouseCursor(juce::MouseCursor::NormalCursor);
            repaint();
        }
    }

    void mouseDown(const juce::MouseEvent& e) override {
        // Note we do NOT open an undo gesture here — a plain click that never turns into a drag
        // would otherwise push an empty undo step. beginGesture happens on the first mouseDrag.
        dragBand = hitTestBand(e.position);
        if (dragBand >= 0) {
            selectedBand = dragBand;
            repaint();
        }
    }

    void mouseDrag(const juce::MouseEvent& e) override {
        if (dragBand < 0)
            return;
        if (!gestureActive) {
            beginGesture();
            gestureActive = true;
        }
        dragBandTo(dragBand, e.position);
    }

    void mouseUp(const juce::MouseEvent&) override {
        if (gestureActive) {
            endGesture();
            gestureActive = false;
        }
        dragBand = -1;
    }

    void mouseDoubleClick(const juce::MouseEvent& e) override {
        const int band = hitTestBand(e.position);
        if (band >= 0)
            removeBand(band);
        else
            addPointAt(e.position);
    }

    void mouseWheelMove(const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override {
        int band = hitTestBand(e.position);
        if (band < 0)
            band = selectedBand;
        if (band < 0 || !eqModule.isBandEnabled(band)) {
            // Nothing under the cursor — let a parent scroll view have the gesture.
            Component::mouseWheelMove(e, wheel);
            return;
        }
        nudgeBandQ(band, wheel.deltaY * kQScrollSensitivity);
    }

    // ---------- painting ----------

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
            const bool hasSignal = updateSpectrum();
            // Repaint while there is signal, plus one final frame once it goes quiet so the
            // display settles instead of freezing mid-decay. A silent patch costs nothing.
            if (hasSignal || spectrumWasActive)
                repaint();
            spectrumWasActive = hasSignal;
        }
    }

    void paint(juce::Graphics& g) override {
        using synth::ui::FrequencyGrid;

        const float w = static_cast<float>(getWidth());
        const float h = static_cast<float>(getHeight());
        if (w <= 0.0f || h <= 0.0f)
            return;

        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());
        const juce::Colour bgColor = lf ? lf->getTheme().colors.bg1 : juce::Colour(0xff1a1a2e);
        const juce::Colour gridColor = lf ? lf->getTheme().colors.border.withAlpha(0.4f) : juce::Colour(0xff2a2a3e);
        const juce::Colour mutedText = lf ? lf->getTheme().colors.textMuted : juce::Colour(0xff6a6a7e);
        const juce::Colour accent = lf ? lf->getTheme().colors.accent : juce::Colour(0xff00b4d8);
        const juce::Colour accent2 = lf ? lf->getTheme().colors.accent2 : juce::Colour(0xff00D1FF);

        g.fillAll(bgColor);

        // ---- Live spectrum, drawn first so it reads as a backdrop behind the curve ----
        if (showSpectrum)
            paintSpectrum(g, w, h, accent2);

        // ---- Grid: log-frequency verticals, dB horizontals ----
        g.setColour(gridColor);
        for (float freq : {50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f})
            g.drawVerticalLine((int)FrequencyGrid::freqToX(freq, w), 0.0f, h);
        for (float db : {-24.0f, -12.0f, 12.0f, 24.0f})
            g.drawHorizontalLine((int)dbToYStatic(db, h), 0.0f, w);

        // The 0 dB line is the reference the whole curve is read against — draw it brighter.
        const float zeroY = dbToYStatic(0.0f, h);
        g.setColour(mutedText.withAlpha(0.55f));
        g.drawHorizontalLine((int)zeroY, 0.0f, w);

        // Axis labels. Only the decade marks get a label, so a narrow card stays readable.
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

        g.setFont(juce::Font(9.0f));
        const struct {
            float db;
            const char* label;
        } dbLabels[] = {{24.0f, "+24"}, {12.0f, "+12"}, {0.0f, "0"}, {-12.0f, "-12"}, {-24.0f, "-24"}};
        for (const auto& lbl : dbLabels)
            g.drawText(lbl.label, 3, (int)dbToYStatic(lbl.db, h) - 6, 28, 12, juce::Justification::left, false);

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

        // ---- Band handles: one numbered dot per ENABLED band ----
        for (int b = 0; b < ParametricEQModule::kNumBands; ++b) {
            const auto& band = lastBands[(size_t)b];
            if (!band.enabled)
                continue;

            const auto centre = bandPosition(band);
            const float bx = juce::jlimit(0.0f, w, centre.x);
            const float by = juce::jlimit(0.0f, h, centre.y);
            const bool active = (b == hoveredBand || b == selectedBand);
            const float radius = active ? kHandleRadius + 1.5f : kHandleRadius;

            // Halo on the hovered/selected handle, so the scroll-changes-Q target is obvious.
            if (active) {
                g.setColour(accent.withAlpha(0.25f));
                g.fillEllipse(bx - radius - 4.0f, by - radius - 4.0f, (radius + 4.0f) * 2.0f, (radius + 4.0f) * 2.0f);
            }
            g.setColour(bgColor);
            g.fillEllipse(bx - radius, by - radius, radius * 2.0f, radius * 2.0f);
            g.setColour(accent);
            g.drawEllipse(bx - radius, by - radius, radius * 2.0f, radius * 2.0f, 2.0f);

            g.setFont(juce::Font(9.0f));
            g.drawText(juce::String(b + 1), (int)(bx - radius), (int)(by - radius), (int)(radius * 2.0f),
                       (int)(radius * 2.0f), juce::Justification::centred, false);
        }

        // ---- Empty state: tell the user how to make a point ----
        if (eqModule.getEnabledBandCount() == 0 && w >= 150.0f) {
            g.setColour(mutedText.withAlpha(0.75f));
            g.setFont(juce::Font(11.0f));
            g.drawText("Double-click to add an EQ point", 0, (int)(h * 0.5f) + 8, (int)w, 16,
                       juce::Justification::centred, false);
        }

        // ---- Readout for the handle being pointed at / dragged ----
        const int readoutBand = (dragBand >= 0) ? dragBand : hoveredBand;
        if (readoutBand >= 0 && lastBands[(size_t)readoutBand].enabled && w >= 150.0f) {
            const auto& band = lastBands[(size_t)readoutBand];
            const juce::String text = FrequencyGrid::formatHzLabel(band.freqHz) + "  " + juce::String(band.gainDb, 1) +
                                      "dB  Q" + juce::String(band.q, 2);
            g.setColour(accent.withAlpha(0.9f));
            g.setFont(juce::Font(10.0f));
            g.drawText(text, 0, (int)h - 15, (int)w - 6, 13, juce::Justification::right, false);
        }
    }

private:
    static constexpr int numPoints = 512;
    static constexpr int fftOrder = 10; // 1024-point FFT
    static constexpr int fftSize = 1 << fftOrder;
    static constexpr float kHandleRadius = 7.0f;
    /** One wheel notch (deltaY == 1) doubles/halves Q at 1.0; scaled down for finer control. */
    static constexpr float kQScrollSensitivity = 1.5f;
    static constexpr float kSpecMinDb = -80.0f;
    static constexpr float kSpecMaxDb = 0.0f;
    /** Peak below this counts as silence, so an idle patch stops repainting the spectrum. */
    static constexpr float kSilenceThreshold = 1.0e-5f;

    void beginGesture() {
        if (onGestureStart)
            onGestureStart();
    }
    void endGesture() {
        if (onGestureEnd)
            onGestureEnd();
    }

    /** Pulls fresh band values from the module and redraws immediately, so a drag tracks the
     *  pointer rather than waiting up to 33 ms for the next timer tick.
     */
    void refreshFromModule() {
        lastBands = eqModule.getBandSnapshots();
        lastOutputGainDb = eqModule.getOutputGainDb();
        recomputeMagnitudes();
        repaint();
    }

    static bool bandsDiffer(const std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>& a,
                            const std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands>& b) {
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].enabled != b[i].enabled || a[i].freqHz != b[i].freqHz || a[i].gainDb != b[i].gainDb ||
                a[i].q != b[i].q)
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

    /** Runs the FFT into spectrumMagnitudes. Returns false when the input is silent, so the
     *  caller can skip the repaint entirely.
     */
    bool updateSpectrum() {
        auto* vb = eqModule.getVisualBuffer();
        if (vb == nullptr)
            return false;

        std::vector<float> samples(vb->getSize(), 0.0f);
        vb->copyTo(samples);

        float peak = 0.0f;
        for (float s : samples)
            peak = std::max(peak, std::abs(s));
        if (peak < kSilenceThreshold)
            return false;

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
        return true;
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

        juce::ColourGradient specGradient(colour.withAlpha(0.22f), 0.0f, 0.0f, colour.withAlpha(0.04f), 0.0f, h, false);
        g.setGradientFill(specGradient);
        g.fillPath(specFill);
        g.setColour(colour.withAlpha(0.45f));
        g.strokePath(specPath, juce::PathStrokeType(1.0f));
    }

    ParametricEQModule& eqModule;

    std::vector<float> magnitudes;
    std::array<ParametricEQModule::BandSnapshot, ParametricEQModule::kNumBands> lastBands{};
    float lastOutputGainDb = 0.0f;

    int selectedBand = -1;
    int hoveredBand = -1;
    int dragBand = -1;
    bool gestureActive = false;

    // The spectrum is the curve's backdrop by design, so it defaults on; the silence gate in
    // updateSpectrum() keeps that free when nothing is playing.
    bool showSpectrum = true;
    bool spectrumWasActive = false;
    juce::dsp::FFT fft{fftOrder};
    juce::dsp::WindowingFunction<float> window{fftSize, juce::dsp::WindowingFunction<float>::hann};
    std::vector<float> fftData;
    std::vector<float> spectrumMagnitudes;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EQCurveComponent)
};
