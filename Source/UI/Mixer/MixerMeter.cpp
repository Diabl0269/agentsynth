// Concern: FRO146 -- MixerMeter's ballistics tick (refresh) and paint only.
#include "MixerMeter.h"

#include "MeterColourStops.h"
#include "MixerMeterScale.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

namespace {

constexpr int kBarGap = 2;
constexpr int kBarsAreaWidth = 16; // two ~7px bars + the gap between them
constexpr int kLabelMinWidth = 14; // roughly enough for "-60" at the meter's tiny tick font

// FRO146: a read-only value interface reporting the meter's louder bar as dBFS text, replacing
// the pre-FRO146 percentage readout -- a percent of a linear 0..1 amplitude meant nothing once the
// scale became dB-linear.
class MeterValueInterface : public juce::AccessibilityValueInterface {
public:
    explicit MeterValueInterface(const MixerMeter& meter)
        : meter_(meter) {}

    bool isReadOnly() const override { return true; }
    double getCurrentValue() const override {
        return (double)std::max(meter_.getDisplayedDbForTest(0), meter_.getDisplayedDbForTest(1));
    }
    juce::String getCurrentValueAsString() const override {
        const float db = (float)getCurrentValue();
        const juce::String text = db <= kMeterMinDb ? juce::String("-inf") : juce::String(db, 1);
        return text + " dBFS";
    }
    void setValue(double) override {}
    void setValueAsString(const juce::String&) override {}
    AccessibleValueRange getRange() const override { return {{(double)kMeterMinDb, (double)kMeterMaxDb}, 0.1}; }

private:
    const MixerMeter& meter_;
};

} // namespace

void MixerMeter::refresh(float elapsedSeconds) {
    bool changed = false;
    for (int leg = 0; leg < 2; ++leg) {
        const float raw = peakProvider ? peakProvider(leg) : 0.0f;
        const float inputDb = meterLinearToDb(raw);
        auto& state = ballistics_[(size_t)leg];
        const float beforeDisplayed = state.displayedDb;
        const float beforeHold = state.peakHoldDb;
        advanceMeterBallistics(state, inputDb, elapsedSeconds);
        if (std::abs(state.displayedDb - beforeDisplayed) >= kRepaintThresholdDb ||
            std::abs(state.peakHoldDb - beforeHold) >= kRepaintThresholdDb)
            changed = true;
    }
    if (changed)
        repaint();
}

std::unique_ptr<juce::AccessibilityHandler> MixerMeter::createAccessibilityHandler() {
    return std::make_unique<juce::AccessibilityHandler>(
        *this, juce::AccessibilityRole::staticText, juce::AccessibilityActions{},
        juce::AccessibilityHandler::Interfaces{std::make_unique<MeterValueInterface>(*this)});
}

void MixerMeter::paint(juce::Graphics& g) {
    const auto* laf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel());
    const synth::theme::Colors fallback{};
    const auto& colors = laf != nullptr ? laf->getTheme().colors : fallback;
    // FRO147: the cached EFFECTIVE stops (the user's pinned override, or the active theme's own
    // four tokens when there is none) -- never rebuilt here, so a Settings > Appearance edit is a
    // single AppLookAndFeel write followed by a repaint, not per-tick work. No AppLookAndFeel
    // installed (a headless test) falls back to the theme literally, same as every other token
    // this function reads through `colors` above.
    const auto stops = laf != nullptr ? laf->getMeterColourStops() : MeterColourStops::fromTheme(fallback);

    auto bounds = getLocalBounds();
    // Tick numbers only where the column has room (docs/mixer/mixer.md meters section) -- below this
    // width the ticks still draw as dashes across the bars, just unlabelled.
    const bool showLabels = bounds.getWidth() >= kBarsAreaWidth + kLabelMinWidth;
    const auto barsArea = showLabels ? bounds.removeFromRight(kBarsAreaWidth) : bounds;
    const auto labelArea = bounds; // whatever showLabels left on the left; empty otherwise

    for (const float db : kMeterTickDb) {
        const float fraction = meterDbToFraction(db);
        const int y = barsArea.getBottom() - juce::roundToInt(fraction * (float)barsArea.getHeight());
        const bool isZeroDb = juce::approximatelyEqual(db, 0.0f);
        g.setColour(colors.border.withAlpha(isZeroDb ? 0.9f : 0.5f));
        g.drawLine((float)barsArea.getX(), (float)y, (float)barsArea.getRight(), (float)y, isZeroDb ? 1.4f : 0.6f);
        if (showLabels) {
            g.setColour(colors.textMuted);
            g.setFont(juce::Font(juce::FontOptions(7.5f)));
            g.drawText(juce::String((int)db), labelArea.getX(), y - 5, labelArea.getWidth(), 10,
                       juce::Justification::centredRight, false);
        }
    }

    const int barWidth = (barsArea.getWidth() - kBarGap) / 2;
    for (int leg = 0; leg < 2; ++leg) {
        auto barBounds = barsArea.withX(barsArea.getX() + leg * (barWidth + kBarGap)).withWidth(barWidth);
        g.setColour(colors.bg1);
        g.fillRect(barBounds);

        const auto& state = ballistics_[(size_t)leg];
        // Positional bands (Cubase/most DAWs' own meter convention), not one whole-bar colour: a
        // bar at +4 dB paints low/mid/high/clip stacked bottom to top; one at -10 dB paints low +
        // part of mid and stops there. See MeterColourStops::forEachBand's own comment.
        if (state.displayedDb > kMeterMinDb) {
            stops.forEachBand(
                kMeterMinDb, state.displayedDb, [&](float bandFromDb, float bandToDb, juce::Colour colour) {
                    const int yBottom = barBounds.getBottom() -
                                        juce::roundToInt(meterDbToFraction(bandFromDb) * (float)barBounds.getHeight());
                    const int yTop = barBounds.getBottom() -
                                     juce::roundToInt(meterDbToFraction(bandToDb) * (float)barBounds.getHeight());
                    g.setColour(colour);
                    g.fillRect(barBounds.getX(), yTop, barBounds.getWidth(), yBottom - yTop);
                });
        }

        // The peak-hold line: a 1px cap drawn at the hold level, coloured for ITS OWN zone (a
        // hold sitting in the clip zone stays visibly red even once the bar itself has released).
        if (state.peakHoldDb > kMeterMinDb) {
            const int holdY = barBounds.getBottom() -
                              juce::roundToInt(meterDbToFraction(state.peakHoldDb) * (float)barBounds.getHeight());
            g.setColour(stops.colourForDb(state.peakHoldDb).brighter(0.4f));
            g.fillRect(barBounds.getX(), juce::jmax(barBounds.getY(), holdY - 1), barBounds.getWidth(), 1);
        }
    }
}

} // namespace synth::ui
