#pragma once

#include "../Modules/VisualBuffer.h"
#include "Theme/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ScopeComponent
    : public juce::Component
    , public juce::Timer {
public:
    ScopeComponent(VisualBuffer& buffer)
        : visualBuffer(buffer) {
        sampleData.resize(buffer.getSize(), 0.0f);
        startTimerHz(60); // higher refresh rate for scope
    }

    ~ScopeComponent() override { stopTimer(); }

    void timerCallback() override {
        visualBuffer.copyTo(sampleData);
        repaint();
    }

    // ---------------------------------------------------------------------------
    // Static helpers (testable without a display)
    // ---------------------------------------------------------------------------

    /** Returns true when the peak magnitude is below the no-signal threshold. */
    static bool isNoSignal(float peak) noexcept { return peak <= 0.02f; }

    /** Maps an amplitude in [-1, 1] to a y-coordinate within bounds.
     *  amp=+1 → top of bounds, amp=-1 → bottom, amp=0 → vertical centre.
     *  Mirrors the waveform mapping used in paint() (45% of height per side). */
    static float amplitudeToY(float amp, juce::Rectangle<float> bounds) noexcept {
        return bounds.getCentreY() - (amp * bounds.getHeight() * 0.45f);
    }

    // ---------------------------------------------------------------------------
    // Paint
    // ---------------------------------------------------------------------------

    void paint(juce::Graphics& g) override {
        juce::Colour bgColor = juce::Colours::black;
        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            bgColor = lf->getTheme().colors.bg1;
        }
        g.fillAll(bgColor);

        auto bounds = getLocalBounds().toFloat();
        auto midY = bounds.getCentreY();
        auto height = bounds.getHeight();
        auto width = bounds.getWidth();

        // --- Derive peak from sample data ---
        float rawPeak = 0.0f;
        for (float s : sampleData)
            rawPeak = std::max(rawPeak, std::abs(s));

        // --- Themed colours (fallback to hardcoded when LnF absent, e.g. tests) ---
        juce::Colour gridColour = juce::Colour(0xff2A2F38);  // border token default
        juce::Colour mutedColour = juce::Colour(0xff5C6470); // textDisabled token default
        juce::Colour waveColour = juce::Colours::limegreen;

        if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
            const auto& colors = lf->getTheme().colors;
            gridColour = colors.border.withAlpha(0.6f);
            mutedColour = colors.textDisabled;
            waveColour = colors.accent;
        }

        // --- Horizontal amplitude grid lines at ±0.5 and ±1.0 ---
        // (centre 0.0 line painted separately below)
        g.setColour(gridColour);
        const float gridAmps[] = {1.0f, 0.5f, -0.5f, -1.0f};
        for (float amp : gridAmps) {
            float y = amplitudeToY(amp, bounds);
            g.drawHorizontalLine(juce::roundToInt(y), bounds.getX(), bounds.getRight());
        }

        // --- Centre (0.0) grid line (slightly brighter to act as baseline) ---
        g.setColour(gridColour.brighter(0.3f));
        g.drawHorizontalLine(juce::roundToInt(midY), bounds.getX(), bounds.getRight());

        // --- No-signal empty state ---
        if (isNoSignal(rawPeak)) {
            g.setColour(mutedColour);
            g.setFont(12.0f);
            g.drawText("No Signal", bounds, juce::Justification::centred, false);
            return;
        }

        // --- Waveform (existing logic, unchanged) ---
        // Auto-scale to fit within 90% of the height, but never scale up more than a 1.0 amplitude signal
        float peak = std::max(rawPeak, 0.01f);
        float dynamicScale = std::min(1.0f, 1.0f / peak);

        g.setColour(waveColour);
        juce::Path p;
        bool first = true;
        for (int i = 0; i < (int)sampleData.size(); ++i) {
            float x = juce::jmap((float)i, 0.0f, (float)sampleData.size(), 0.0f, width);
            float y = midY - (sampleData[i] * dynamicScale * height * 0.45f);

            if (first) {
                p.startNewSubPath(x, y);
                first = false;
            } else {
                p.lineTo(x, y);
            }
        }

        g.strokePath(p, juce::PathStrokeType(1.5f));
    }

private:
    VisualBuffer& visualBuffer;
    std::vector<float> sampleData;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeComponent)
};
