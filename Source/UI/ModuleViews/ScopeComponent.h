#pragma once

#include "Modules/VisualBuffer.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include <juce_gui_basics/juce_gui_basics.h>

class ScopeComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer {
public:
    ScopeComponent(VisualBuffer& buffer)
        : visualBuffer(buffer) {
        sampleData.resize(buffer.getSize(), 0.0f);
        incoming.resize(sampleData.size(), 0.0f);
        setTitle("Oscilloscope");
        setDescription("Live waveform of this module's output");
        setTooltip("Oscilloscope: the module's output waveform");
        syncTimer();
    }

    ~ScopeComponent() override { stopTimer(); }

    // A card's scope is hidden until its Show scope toggle is on, and most cards never show it: a hidden scope runs no
    // timer at all, and a shown one repaints only when the waveform changed (a silent module's flat line is drawn
    // once).
    void visibilityChanged() override { syncTimer(); }

    void timerCallback() override {
        for (auto* c = getParentComponent(); c != nullptr; c = c->getParentComponent())
            if (!c->isVisible()) // a card hidden inside a collapsed macro
                return;
        visualBuffer.markWatched(); // a module that sleeps while unwatched renders again for a shown scope
        visualBuffer.copyTo(incoming);
        if (incoming == sampleData)
            return;
        sampleData.swap(incoming);
        ++repaintRequests_;
        repaint();
    }

    /** Test seam: how many ticks found a changed waveform and asked for a repaint. */
    int repaintRequests() const noexcept { return repaintRequests_; }

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

        // --- Themed colours ---
        const auto& colors = synth::theme::themeOf(*this).colors;
        const auto gridColour = colors.border.withAlpha(0.6f);
        const auto mutedColour = colors.textDisabled;
        const auto waveColour = colors.accent;

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
    void syncTimer() {
        if (!isVisible())
            stopTimer();
        else if (!isTimerRunning())
            startTimerHz(60); // higher refresh rate for scope
    }

    VisualBuffer& visualBuffer;
    std::vector<float> sampleData;
    std::vector<float> incoming;
    int repaintRequests_ = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ScopeComponent)
};
