#pragma once

#include "Modules/GainReductionMeterSource.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Live gain-reduction readout for a Compressor or Limiter card: a horizontal bar that fills from the
 *  left as the module pulls the level down (0 dB empty, kMaxDb full), with the reading in decibels.
 *
 *  Keyboard and screen reader: read-only, so not a Tab stop. It carries a title, description and
 *  tooltip, like ThresholdControlComponent's meter.
 *
 *  Repaint discipline: owns its own timer and repaints *itself* only when the displayed reading
 *  (rounded to a tenth of a dB, which is also about a pixel of bar) changes. It is a separate
 *  component rather than something ModuleComponent::paint draws, because ModuleComponent is
 *  setBufferedToImage(true): painting the meter there would invalidate that cached image on every tick
 *  (docs/layout/rendering.md). The timer only runs while the card is on screen.
 */
class GainReductionMeterComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer {
public:
    explicit GainReductionMeterComponent(GainReductionMeterSource& sourceToWatch)
        : source(sourceToWatch) {
        setTitle("Gain reduction meter");
        setDescription("How far the module is turning the level down right now, in decibels");
        setTooltip("Gain reduction: how far the level is being turned down right now");
        setInterceptsMouseClicks(false, false);
        startTimerHz(kRefreshHz);
    }

    ~GainReductionMeterComponent() override { stopTimer(); }

    static constexpr int getHeight() noexcept { return kHeight; }
    static constexpr float getMaxDecibels() noexcept { return kMaxDb; }

    /** Maps a reduction in decibels onto [0, 1] along the bar. */
    static float reductionToNormalized(float reductionDb) noexcept {
        return juce::jlimit(0.0f, 1.0f, reductionDb / kMaxDb);
    }

    /** The reading as the meter shows it: tenths of a dB, never negative. */
    static float quantise(float reductionDb) noexcept {
        return std::round(juce::jmax(0.0f, reductionDb) * 10.0f) * 0.1f;
    }

    /** True when a repaint is warranted; mirrors timerCallback's gate so "no repaint at rest" is testable. */
    static bool needsRepaint(float oldReductionDb, float newReductionDb) noexcept {
        return quantise(oldReductionDb) != quantise(newReductionDb);
    }

    static juce::String readoutText(float reductionDb) {
        const float shown = quantise(reductionDb);
        return shown <= 0.0f ? juce::String("0.0 dB") : "-" + juce::String(shown, 1) + " dB";
    }

    float getShownReductionDb() const noexcept { return reductionDb; }

    void visibilityChanged() override { updateTimer(); }
    void parentHierarchyChanged() override { updateTimer(); }

    void timerCallback() override {
        const float latest = source.getGainReductionDb();
        if (!needsRepaint(reductionDb, latest))
            return;
        reductionDb = latest;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        juce::Colour bg = juce::Colour(0xff14171C);
        juce::Colour barColour = juce::Colours::orange;
        juce::Colour textColour = juce::Colours::lightgrey;
        juce::Colour captionColour = juce::Colours::lightgrey;
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bg = c.bg1;
            barColour = c.gateWire;
            textColour = c.textPrimary;
            captionColour = c.textMuted;
        }

        auto bounds = getLocalBounds().toFloat();
        g.setColour(captionColour);
        g.setFont(11.0f);
        g.drawText("Gain reduction", bounds.removeFromTop((float)kCaptionHeight).toNearestInt(),
                   juce::Justification::centredLeft, false);

        const auto bar = bounds.reduced(1.0f, 0.0f);
        g.setColour(bg);
        g.fillRoundedRectangle(bar, 2.0f);
        const float fill = reductionToNormalized(reductionDb) * bar.getWidth();
        if (fill > 0.5f) {
            g.setColour(barColour);
            g.fillRoundedRectangle(bar.withWidth(fill), 2.0f);
        }
        g.setColour(textColour);
        g.setFont(10.0f);
        g.drawText(readoutText(reductionDb), bar.reduced(4.0f, 0.0f).toNearestInt(), juce::Justification::centredRight,
                   false);
    }

private:
    void updateTimer() {
        if (isShowing()) {
            if (!isTimerRunning())
                startTimerHz(kRefreshHz);
        } else {
            stopTimer();
        }
    }

    static constexpr int kRefreshHz = 20;
    static constexpr float kMaxDb = 24.0f;
    static constexpr int kCaptionHeight = 14;
    static constexpr int kBarHeight = 14;
    static constexpr int kHeight = kCaptionHeight + kBarHeight;

    GainReductionMeterSource& source;
    float reductionDb = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GainReductionMeterComponent)
};
