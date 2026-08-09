#pragma once

#include "../Modules/SampleHoldModule.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

/** Live trigger-level readout for SampleHoldModule.
 *
 *  Draws the incoming Trigger jack level as a bipolar bar (-1 left, 0 centre, +1 right) with a
 *  draggable-looking marker at the current threshold, so the threshold can be set by eye against
 *  the actual signal rather than guessed.
 *
 *  Repaint discipline: this owns its own timer and repaints *itself* only when a displayed value
 *  moves past a visible amount. It is deliberately a separate component rather than something
 *  ModuleComponent::paint draws, because ModuleComponent is setBufferedToImage(true) — painting
 *  the meter there would invalidate that cached image and re-run the module's expensive text
 *  layout on every tick, which is the repaint storm docs/layout.md warns about.
 */
class TriggerMeterComponent
    : public juce::Component
    , public juce::Timer {
public:
    explicit TriggerMeterComponent(SampleHoldModule& moduleToWatch)
        : module(moduleToWatch) {
        startTimerHz(kRefreshHz);
    }

    ~TriggerMeterComponent() override { stopTimer(); }

    // -------------------------------------------------------------------------
    // Static helpers (pure — unit-testable without a display)
    // -------------------------------------------------------------------------

    /** Maps a bipolar value in [-1, 1] to an x offset within a width. */
    static float valueToX(float value, float x, float width) noexcept {
        return x + (juce::jlimit(-1.0f, 1.0f, value) + 1.0f) * 0.5f * width;
    }

    /** Number of frames the fired-flash stays lit after a capture. */
    static constexpr int getFlashFrames() noexcept { return kFlashFrames; }

    /** True when a repaint is warranted given old/new readings. Mirrors timerCallback's gate so
     *  the "don't repaint on idle" rule can be tested without a message loop. */
    static bool needsRepaint(float oldLevel, float newLevel, float oldThreshold, float newThreshold, bool oldHigh,
                             bool newHigh, int oldCount, int newCount, int flashRemaining) noexcept {
        if (oldHigh != newHigh || oldCount != newCount || flashRemaining > 0)
            return true;
        return std::abs(oldLevel - newLevel) > kEpsilon || std::abs(oldThreshold - newThreshold) > kEpsilon;
    }

    // -------------------------------------------------------------------------

    void timerCallback() override {
        const float newLevel = module.getTriggerLevel();
        const float newThreshold = module.getEffectiveThreshold();
        const bool newHigh = module.isTriggerHigh();
        const int newCount = module.getTriggerCount();

        if (newCount != count)
            flashRemaining = kFlashFrames;
        else if (flashRemaining > 0)
            --flashRemaining;

        if (!needsRepaint(level, newLevel, threshold, newThreshold, high, newHigh, count, newCount, flashRemaining))
            return;

        level = newLevel;
        threshold = newThreshold;
        high = newHigh;
        count = newCount;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        auto bounds = getLocalBounds().toFloat().reduced(1.0f);

        juce::Colour bg = juce::Colour(0xff14171C);
        juce::Colour gridColour = juce::Colour(0xff2A2F38);
        juce::Colour barColour = juce::Colours::grey;
        juce::Colour armedColour = juce::Colours::orange;
        juce::Colour markerColour = juce::Colours::white;
        juce::Colour mutedColour = juce::Colour(0xff5C6470);

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bg = c.bg1;
            gridColour = c.border.withAlpha(0.6f);
            barColour = c.textMuted;
            armedColour = c.gateWire;
            markerColour = c.accent;
            mutedColour = c.textDisabled;
        }

        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 2.0f);

        const float centreX = valueToX(0.0f, bounds.getX(), bounds.getWidth());

        // Zero baseline.
        g.setColour(gridColour);
        g.drawVerticalLine(juce::roundToInt(centreX), bounds.getY(), bounds.getBottom());

        // Level bar, drawn from centre toward the signal's sign.
        const float levelX = valueToX(level, bounds.getX(), bounds.getWidth());
        const float barTop = bounds.getY() + bounds.getHeight() * 0.25f;
        const float barHeight = bounds.getHeight() * 0.5f;
        g.setColour(high ? armedColour : barColour);
        g.fillRect(juce::Rectangle<float>(std::min(centreX, levelX), barTop, std::abs(levelX - centreX), barHeight));

        // Threshold marker.
        const float thresholdX = valueToX(threshold, bounds.getX(), bounds.getWidth());
        g.setColour(markerColour);
        g.fillRect(juce::Rectangle<float>(thresholdX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));

        // Fired flash — a filled pip at the right edge on each capture.
        if (flashRemaining > 0) {
            g.setColour(armedColour.withAlpha(juce::jlimit(0.0f, 1.0f, (float)flashRemaining / (float)kFlashFrames)));
            g.fillEllipse(bounds.getRight() - 7.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        }

        // Idle hint when nothing is patched into the Trigger jack.
        if (std::abs(level) <= kEpsilon) {
            g.setColour(mutedColour);
            g.setFont(10.0f);
            g.drawText("no trigger", bounds, juce::Justification::centredLeft, false);
        }
    }

private:
    static constexpr int kRefreshHz = 20;
    static constexpr int kFlashFrames = 4;
    static constexpr float kEpsilon = 0.01f;

    SampleHoldModule& module;
    float level = 0.0f;
    float threshold = 0.5f;
    bool high = false;
    int count = 0;
    int flashRemaining = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TriggerMeterComponent)
};
