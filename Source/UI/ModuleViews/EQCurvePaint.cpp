// EQCurvePaint.cpp -- how EQCurveComponent draws: grid, response curve, band dots, the floating value readout, and
// the live spectrum behind them. The curve is declared in EQCurveComponent.h.

#include "EQCurveComponent.h"
#include "EQCurveHoldState.h"

#include "EQBandKinds.h"
#include "Modules/VisualBuffer.h"
#include "UI/Layout/ActionGlyphs.h"
#include "UI/Layout/CentredLabel.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <algorithm>
#include <cmath>

using synth::ui::FrequencyGrid;

namespace {
constexpr float kReadoutHeight = 16.0f;
constexpr float kReadoutGap = 8.0f;
constexpr float kReadoutGlyphPillW = 26.0f;
} // namespace

juce::Colour EQCurveComponent::dotColour(int band) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto now = synth::ui::eqBandKindColour(colors, synth::ui::eqBandKindOf(dotBands_[(size_t)band].type));
    const auto before = synth::ui::eqBandKindColour(colors, synth::ui::eqBandKindOf(morphFromType_[(size_t)band]));
    return now.interpolatedWith(before, juce::jlimit(0.0f, 1.0f, typeMorph_));
}

juce::Colour EQCurveComponent::dotRingColour(int band) const {
    return synth::ui::eqDotRingColour(synth::theme::themeOf(*this).colors, dotColour(band));
}

juce::Colour EQCurveComponent::getDotColourForTest(int band) const { return dotColour(band); }
juce::Colour EQCurveComponent::getDotRingColourForTest(int band) const { return dotRingColour(band); }

juce::Rectangle<float> EQCurveComponent::plotArea() const {
    const auto bounds = getLocalBounds().toFloat();
    const float inset = synth::ui::focusRingInset(synth::theme::themeOf(*this));
    return bounds.getWidth() > 2.0f * inset + 8.0f && bounds.getHeight() > 2.0f * inset + 8.0f ? bounds.reduced(inset)
                                                                                               : bounds;
}

void EQCurveComponent::paint(juce::Graphics& g) {
    if (getWidth() <= 0 || getHeight() <= 0)
        return;
    const auto plot = plotArea();
    const float w = plot.getWidth();
    const float h = plot.getHeight();

    const auto& colors = synth::theme::themeOf(*this).colors;
    g.fillAll(colors.bg1);

    {
        // Grid, spectrum and curve are drawn in the plot's own coordinates, so none of them reaches the focus ring.
        juce::Graphics::ScopedSaveState plotState(g);
        // The response runs past the plot's top and bottom for a steep or extreme band: the plot clips it.
        g.reduceClipRegion(plot.getSmallestIntegerContainer());
        g.addTransform(juce::AffineTransform::translation(plot.getX(), plot.getY()));
        // The spectrum is drawn first so it reads as a backdrop behind the curve.
        if (showSpectrum)
            paintSpectrum(g, w, h, colors.accent2);
        paintGrid(g, w, h);
        paintCurve(g, w, h);
        if (eqModule.getEnabledBandCount() == 0 && w >= 150.0f) {
            g.setColour(colors.textMuted.withAlpha(0.75f));
            g.setFont(juce::Font(11.0f));
            g.drawText("Double-click to add an EQ point", 0, (int)(h * 0.5f) + 8, (int)w, 16,
                       juce::Justification::centred, false);
        }
    }
    paintDots(g, plot);

    if (getWidth() >= 150) {
        for (int b = 0; b < ParametricEQModule::kMaxBands; ++b)
            if (const float look = getReadoutLook(b); look > 0.0f && lastBands[(size_t)b].enabled)
                paintReadout(g, b, plot, look);
    }

    hold_->box.paintMarquee(g);
    synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this);
}

void EQCurveComponent::paintGrid(juce::Graphics& g, float w, float h) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    g.setColour(colors.border.withAlpha(0.4f));
    for (float freq : {50.0f, 100.0f, 500.0f, 1000.0f, 5000.0f, 10000.0f})
        g.drawVerticalLine((int)FrequencyGrid::freqToX(freq, w), 0.0f, h);
    for (float db : {-24.0f, -12.0f, 12.0f, 24.0f})
        g.drawHorizontalLine((int)dbToYStatic(db, h), 0.0f, w);

    // The 0 dB line is the reference the whole curve is read against, so it is drawn brighter.
    g.setColour(colors.textMuted.withAlpha(0.55f));
    g.drawHorizontalLine((int)dbToYStatic(0.0f, h), 0.0f, w);

    // Only the decade marks get a label, so a narrow card stays readable.
    g.setFont(juce::Font(9.5f));
    g.setColour(colors.textMuted);
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
}

void EQCurveComponent::paintCurve(juce::Graphics& g, float w, float h) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const float zeroY = dbToYStatic(0.0f, h);
    const float clampedZeroY = juce::jlimit(0.0f, h, zeroY);
    juce::Path curvePath;
    juce::Path fillPath;
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

    g.setColour(colors.accent.withAlpha(0.22f));
    g.fillPath(fillPath);
    g.setColour(colors.accent);
    g.strokePath(curvePath, juce::PathStrokeType(2.0f));
}

// One numbered dot per ENABLED band: the ring takes the band's kind colour, the selected or hovered dot a halo in the
// accent, the soloed one a bright outer ring while the others dim. A dot that is arriving or leaving (alpha below 1) is
// smaller, so it grows from or shrinks into the curve.
void EQCurveComponent::paintDots(juce::Graphics& g, juce::Rectangle<float> plot) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    for (int b = 0; b < ParametricEQModule::kMaxBands; ++b) {
        const auto& band = dotBands_[(size_t)b];
        if (!band.enabled)
            continue;
        const auto soloLook = getSoloLook(b);
        const float dotAlpha = dotAlpha_[(size_t)b] * (1.0f - kSoloDim * soloLook.y);
        const auto centre = bandPosition(band);
        const float bx = juce::jlimit(plot.getX(), plot.getRight(), centre.x);
        const float by = juce::jlimit(plot.getY(), plot.getBottom(), centre.y);
        const float lit = getHaloLook(b);
        const float radius = (kHandleRadius + kDotActiveGrow * lit) * (0.35f + 0.65f * dotAlpha);

        if (lit > 0.0f) {
            const float halo = radius + kDotHaloWidth;
            g.setColour(colors.accent.withAlpha(kDotHaloAlpha * dotAlpha * lit));
            g.fillEllipse(bx - halo, by - halo, halo * 2.0f, halo * 2.0f);
            // The accent ring stands just outside the dot's own ring, so the selection reads without the halo.
            const float accentRing = radius + 1.0f + kDotAccentRingPx * 0.5f;
            g.setColour(colors.accent.withAlpha(dotAlpha * lit));
            g.drawEllipse(bx - accentRing, by - accentRing, accentRing * 2.0f, accentRing * 2.0f, kDotAccentRingPx);
        }
        if (soloLook.x > 0.0f) {
            const float outer = radius + 5.0f * soloLook.x;
            g.setColour(colors.textPrimary.withAlpha(soloLook.x * dotAlpha_[(size_t)b]));
            g.drawEllipse(bx - outer, by - outer, outer * 2.0f, outer * 2.0f, 2.0f);
        }
        g.setColour(colors.bg1.withMultipliedAlpha(dotAlpha));
        g.fillEllipse(bx - radius, by - radius, radius * 2.0f, radius * 2.0f);
        g.setColour(dotRingColour(b).withMultipliedAlpha(dotAlpha));
        g.drawEllipse(bx - radius, by - radius, radius * 2.0f, radius * 2.0f, 2.0f);

        g.setColour(colors.textPrimary.withMultipliedAlpha(dotAlpha));
        synth::ui::drawLabelCentredOn(g, juce::String(b + 1), juce::Font(kDotNumberPt), {bx, by});
    }
}

// The floating readout above a dot: the type as a pill in its kind colour, then frequency, gain and Q. It flips below
// the dot near the top edge and stays inside the component sideways.
void EQCurveComponent::paintReadout(juce::Graphics& g, int band, juce::Rectangle<float> plot, float look) const {
    const auto& colors = synth::theme::themeOf(*this).colors;
    const auto& snap = dotBands_[(size_t)band];
    const juce::Font font(10.0f);
    const auto values = FrequencyGrid::formatHzLabel(snap.freqHz) + "  " + juce::String(snap.gainDb, 1) + "dB  Q" +
                        juce::String(snap.q, 2);
    const float typeW = kReadoutGlyphPillW;
    const float valuesW = juce::GlyphArrangement::getStringWidth(font, values) + 8.0f;
    const float total = typeW + valuesW + 4.0f;

    const auto centre = bandPosition(snap);
    const float x = juce::jlimit(plot.getX() + 2.0f, std::max(plot.getX() + 2.0f, plot.getRight() - total - 2.0f),
                                 centre.x - total * 0.5f);
    float y = centre.y - kHandleRadius - kReadoutGap - kReadoutHeight;
    if (y < plot.getY() + 2.0f)
        y = centre.y + kHandleRadius + kReadoutGap;
    y = juce::jlimit(plot.getY() + 2.0f, std::max(plot.getY() + 2.0f, plot.getBottom() - kReadoutHeight - 2.0f), y);

    const float alpha = dotAlpha_[(size_t)band] * look;
    const juce::Rectangle<float> box(x, y, total, kReadoutHeight);
    g.setColour(colors.surface.withAlpha(0.92f * alpha));
    g.fillRoundedRectangle(box, kReadoutHeight * 0.5f);
    g.setColour(colors.border.withAlpha(alpha));
    g.drawRoundedRectangle(box.reduced(0.5f), kReadoutHeight * 0.5f, 1.0f);

    const juce::Rectangle<float> pill(x + 2.0f, y + 2.0f, typeW, kReadoutHeight - 4.0f);
    g.setColour(dotColour(band).withAlpha(alpha));
    g.fillRoundedRectangle(pill, pill.getHeight() * 0.5f);
    // The type is a response-curve glyph; while the type morphs the old one fades out as the new one fades in.
    const auto glyphArea = juce::Rectangle<float>(14.0f, 10.0f).withCentre(pill.getCentre());
    const auto glyphInk = synth::ui::eqBandKindInk(colors).withAlpha(alpha);
    const float morph = juce::jlimit(0.0f, 1.0f, typeMorph_);
    if (morph > 0.0f && morphFromType_[(size_t)band] != snap.type)
        synth::ui::paintEqBandTypeGlyph(g, glyphArea, glyphInk.withMultipliedAlpha(morph),
                                        morphFromType_[(size_t)band]);
    synth::ui::paintEqBandTypeGlyph(g, glyphArea, glyphInk.withMultipliedAlpha(1.0f - morph), snap.type);
    g.setFont(font);
    g.setColour(colors.textPrimary.withAlpha(alpha));
    g.drawText(values, juce::Rectangle<float>(x + typeW + 4.0f, y, valuesW, kReadoutHeight).toNearestInt(),
               juce::Justification::centred, false);
}

bool EQCurveComponent::spectrumTick() {
    if (!showSpectrum || eqModule.getVisualBuffer() == nullptr)
        return false;
    const bool hasSignal = updateSpectrum();
    // Repaint while the trace moves (signal, or falling to the floor), plus one final frame when it comes to rest so
    // the display settles on the floor instead of freezing mid-decay. A silent patch costs nothing.
    const bool paintNow = hasSignal || spectrumWasActive;
    if (paintNow)
        repaint();
    spectrumWasActive = hasSignal;
    return paintNow;
}

bool EQCurveComponent::updateSpectrum() {
    auto* vb = eqModule.getVisualBuffer();
    if (vb == nullptr)
        return false;

    std::vector<float> samples(vb->getSize(), 0.0f);
    vb->copyTo(samples);

    float peak = 0.0f;
    for (float s : samples)
        peak = std::max(peak, std::abs(s));
    if (peak < kSilenceThreshold) {
        // Silence is a reading too: the trace falls to the floor instead of keeping the last picture. True while it
        // still moves; once every point rests, an idle patch costs nothing.
        bool moving = false;
        for (auto& m : spectrumMagnitudes) {
            if (m <= kSpecMinDb)
                continue;
            m += kSpectrumSmoothing * (kSpecMinDb - m);
            m = m < kSpecMinDb + kSpectrumRestDb ? kSpecMinDb : m;
            moving = moving || m > kSpecMinDb;
        }
        return moving;
    }

    std::fill(fftData.begin(), fftData.end(), 0.0f);
    const int copySize = std::min((int)samples.size(), fftSize);
    for (int i = 0; i < copySize; ++i)
        fftData[(size_t)i] = samples[(size_t)i];

    window.multiplyWithWindowingTable(fftData.data(), fftSize);
    fft.performFrequencyOnlyForwardTransform(fftData.data());

    const float binWidth = static_cast<float>(eqModule.getLastSampleRate()) / static_cast<float>(fftSize);
    const float normFactor = 2.0f / static_cast<float>(fftSize);

    for (int i = 0; i < numPoints; ++i) {
        const float freq = FrequencyGrid::indexToFreq(i, numPoints);
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
        spectrumMagnitudes[(size_t)i] += kSpectrumSmoothing * (target - spectrumMagnitudes[(size_t)i]);
    }
    return true;
}

void EQCurveComponent::paintSpectrum(juce::Graphics& g, float w, float h, juce::Colour colour) {
    juce::Path specPath;
    juce::Path specFill;
    bool first = true;
    for (int i = 0; i < numPoints; ++i) {
        const float x = FrequencyGrid::freqToX(FrequencyGrid::indexToFreq(i, numPoints), w);
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

    juce::ColourGradient specGradient(colour.withAlpha(synth::ui::kEqSpectrumFillAlpha), 0.0f, 0.0f,
                                      colour.withAlpha(0.04f), 0.0f, h, false);
    g.setGradientFill(specGradient);
    g.fillPath(specFill);
    g.setColour(colour.withAlpha(0.45f));
    g.strokePath(specPath, juce::PathStrokeType(1.0f));
}
