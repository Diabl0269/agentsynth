#pragma once

#include "../Modules/ThresholdMeterSource.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

/** Live threshold readout, optionally with an attached slider.
 *
 *  Two presentations, one component:
 *
 *  - Meter-only (no parameter): a thin bar matching the original Sample & Hold trigger
 *    meter. The module keeps its own rotary for Threshold.
 *  - Slider+meter: a caption, a level bar, and a linear slider whose thumb is the
 *    threshold "slice". The bar fills to the current input so the slice can be set by
 *    eye. Used by ADSR and Comparator; Compressor / Limiter can adopt the Decibels
 *    scale later without a new widget.
 *
 *  Repaint discipline: this owns its own timer and repaints *itself* only when a
 *  displayed value moves past a visible amount. It is deliberately a separate
 *  component rather than something ModuleComponent::paint draws, because
 *  ModuleComponent is setBufferedToImage(true) — painting the meter there would
 *  invalidate that cached image on every tick (see docs/layout.md §10).
 */
class ThresholdControlComponent
    : public juce::Component
    , public juce::Timer {
public:
    explicit ThresholdControlComponent(ThresholdMeterSource& sourceToWatch)
        : ThresholdControlComponent(sourceToWatch, nullptr) {}

    ThresholdControlComponent(ThresholdMeterSource& sourceToWatch, juce::AudioParameterFloat* thresholdParam)
        : source(sourceToWatch)
        , scale(sourceToWatch.getThresholdScale())
        , minDb(sourceToWatch.getThresholdMinDecibels()) {
        if (thresholdParam != nullptr) {
            slider = std::make_unique<juce::Slider>();
            slider->setComponentID(thresholdParam->getName(100));
            slider->setSliderStyle(juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle(juce::Slider::TextBoxRight, false, 44, 16);
            slider->setName(thresholdParam->getName(100));
            addAndMakeVisible(*slider);
            attachment = std::make_unique<juce::SliderParameterAttachment>(*thresholdParam, *slider);
        }
        startTimerHz(kRefreshHz);
    }

    ~ThresholdControlComponent() override { stopTimer(); }

    int getPreferredHeight() const noexcept { return slider != nullptr ? kSliderModeHeight : kMeterOnlyHeight; }

    juce::Slider* getSlider() const noexcept { return slider.get(); }

    juce::String getParamName() const { return slider != nullptr ? slider->getComponentID() : juce::String(); }

    // -------------------------------------------------------------------------
    // Static helpers (pure — unit-testable without a display)
    // -------------------------------------------------------------------------

    /** Maps a native-scale value onto [0, 1] along the meter. */
    static float valueToNormalized(float value, ThresholdScale scale, float minDb = -60.0f) noexcept {
        switch (scale) {
        case ThresholdScale::Unipolar:
            return juce::jlimit(0.0f, 1.0f, value);
        case ThresholdScale::Decibels: {
            const float floorDb = minDb < 0.0f ? minDb : -60.0f;
            const float span = 0.0f - floorDb;
            if (span <= 0.0f)
                return 0.0f;
            return juce::jlimit(0.0f, 1.0f, (value - floorDb) / span);
        }
        case ThresholdScale::Bipolar:
        default:
            return (juce::jlimit(-1.0f, 1.0f, value) + 1.0f) * 0.5f;
        }
    }

    /** Maps a native-scale value to an x offset within a width. */
    static float valueToX(float value, float x, float width, ThresholdScale scale = ThresholdScale::Bipolar,
                          float minDb = -60.0f) noexcept {
        return x + valueToNormalized(value, scale, minDb) * width;
    }

    static constexpr int getFlashFrames() noexcept { return kFlashFrames; }

    static constexpr int getMeterOnlyHeight() noexcept { return kMeterOnlyHeight; }

    static constexpr int getSliderModeHeight() noexcept { return kSliderModeHeight; }

    /** True when a repaint is warranted given old/new readings. Mirrors timerCallback's gate so
     *  the "don't repaint on idle" rule can be tested without a message loop. */
    static bool needsRepaint(float oldLevel, float newLevel, float oldThreshold, float newThreshold, bool oldHigh,
                             bool newHigh, int oldCount, int newCount, int flashRemaining) noexcept {
        if (oldHigh != newHigh || oldCount != newCount || flashRemaining > 0)
            return true;
        return std::abs(oldLevel - newLevel) > kEpsilon || std::abs(oldThreshold - newThreshold) > kEpsilon;
    }

    // -------------------------------------------------------------------------

    void resized() override {
        if (slider == nullptr)
            return;
        auto bounds = getLocalBounds();
        bounds.removeFromTop(kCaptionHeight);
        bounds.removeFromTop(kAttachedMeterHeight + kMeterSliderGap);
        slider->setBounds(bounds);
    }

    void timerCallback() override {
        const float newLevel = source.getMeterLevel();
        const float newThreshold = source.getEffectiveThreshold();
        const bool newHigh = source.isOverThreshold();
        const int newCount = source.getTriggerCount();

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
        juce::Colour bg = juce::Colour(0xff14171C);
        juce::Colour gridColour = juce::Colour(0xff2A2F38);
        juce::Colour barColour = juce::Colours::grey;
        juce::Colour armedColour = juce::Colours::orange;
        juce::Colour markerColour = juce::Colours::white;
        juce::Colour mutedColour = juce::Colour(0xff5C6470);
        juce::Colour captionColour = juce::Colours::lightgrey;

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& c = lf->getTheme().colors;
            bg = c.bg1;
            gridColour = c.border.withAlpha(0.6f);
            barColour = c.textMuted;
            armedColour = c.gateWire;
            markerColour = c.accent;
            mutedColour = c.textDisabled;
            captionColour = c.textMuted;
        }

        auto bounds = getLocalBounds().toFloat();
        if (slider != nullptr) {
            g.setColour(captionColour);
            g.setFont(11.0f);
            g.drawText(slider->getName(), bounds.removeFromTop((float)kCaptionHeight).toNearestInt(),
                       juce::Justification::centredLeft, false);
            auto meter = bounds.removeFromTop((float)kAttachedMeterHeight).reduced(1.0f, 0.0f);
            paintMeter(g, meter, bg, gridColour, barColour, armedColour, markerColour, mutedColour);
            return;
        }

        paintMeter(g, bounds.reduced(1.0f), bg, gridColour, barColour, armedColour, markerColour, mutedColour);
    }

private:
    void paintMeter(juce::Graphics& g, juce::Rectangle<float> bounds, juce::Colour bg, juce::Colour gridColour,
                    juce::Colour barColour, juce::Colour armedColour, juce::Colour markerColour,
                    juce::Colour mutedColour) const {
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 2.0f);

        const float width = bounds.getWidth();
        const float x = bounds.getX();

        if (scale == ThresholdScale::Bipolar) {
            const float centreX = valueToX(0.0f, x, width, scale, minDb);
            g.setColour(gridColour);
            g.drawVerticalLine(juce::roundToInt(centreX), bounds.getY(), bounds.getBottom());

            const float levelX = valueToX(level, x, width, scale, minDb);
            const float barTop = bounds.getY() + bounds.getHeight() * 0.25f;
            const float barHeight = bounds.getHeight() * 0.5f;
            g.setColour(high ? armedColour : barColour);
            g.fillRect(
                juce::Rectangle<float>(std::min(centreX, levelX), barTop, std::abs(levelX - centreX), barHeight));
        } else {
            const float zeroX = valueToX(scale == ThresholdScale::Decibels ? minDb : 0.0f, x, width, scale, minDb);
            const float levelX = valueToX(level, x, width, scale, minDb);
            const float barTop = bounds.getY() + bounds.getHeight() * 0.25f;
            const float barHeight = bounds.getHeight() * 0.5f;
            g.setColour(high ? armedColour : barColour);
            g.fillRect(juce::Rectangle<float>(zeroX, barTop, std::max(0.0f, levelX - zeroX), barHeight));
        }

        const float thresholdX = valueToX(threshold, x, width, scale, minDb);
        g.setColour(markerColour);
        g.fillRect(juce::Rectangle<float>(thresholdX - 1.0f, bounds.getY(), 2.0f, bounds.getHeight()));

        if (flashRemaining > 0) {
            g.setColour(armedColour.withAlpha(juce::jlimit(0.0f, 1.0f, (float)flashRemaining / (float)kFlashFrames)));
            g.fillEllipse(bounds.getRight() - 7.0f, bounds.getCentreY() - 3.0f, 6.0f, 6.0f);
        }

        if (isIdleLevel(level, scale, minDb)) {
            g.setColour(mutedColour);
            g.setFont(10.0f);
            g.drawText(source.getMeterIdleLabel(), bounds.reduced(4.0f, 0.0f).toNearestInt(),
                       juce::Justification::centredLeft, false);
        }
    }

    static bool isIdleLevel(float value, ThresholdScale scale, float minDb) noexcept {
        if (scale == ThresholdScale::Decibels)
            return value <= minDb + 0.5f;
        return std::abs(value) <= kEpsilon;
    }

    static constexpr int kRefreshHz = 20;
    static constexpr int kFlashFrames = 4;
    static constexpr float kEpsilon = 0.01f;
    static constexpr int kMeterOnlyHeight = 18;
    static constexpr int kCaptionHeight = 14;
    static constexpr int kAttachedMeterHeight = 12;
    static constexpr int kMeterSliderGap = 2;
    static constexpr int kSliderRowHeight = 20;
    static constexpr int kSliderModeHeight = kCaptionHeight + kAttachedMeterHeight + kMeterSliderGap + kSliderRowHeight;

    ThresholdMeterSource& source;
    ThresholdScale scale;
    float minDb;
    std::unique_ptr<juce::Slider> slider;
    std::unique_ptr<juce::SliderParameterAttachment> attachment;
    float level = 0.0f;
    float threshold = 0.5f;
    bool high = false;
    int count = 0;
    int flashRemaining = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThresholdControlComponent)
};

/** Back-compat name for the Sample & Hold tests and docs that still mention it. */
using TriggerMeterComponent = ThresholdControlComponent;
