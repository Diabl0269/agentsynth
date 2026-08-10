#pragma once

#include "../Modules/WavetableOscillatorModule.h"
#include "Theme/AppLookAndFeel.h"
#include "Theme/Theme.h"
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/**
    Draws the wavetable frame currently under the scan position, with a few frames from
    either side receding behind it so the stack reads as three-dimensional.

    Repaints are gated: the timer only calls repaint() when the table selection, the frame
    count or the scan position actually changed (see docs/layout.md §10-11 — no
    unconditional per-tick repaints).
*/
class WavetableDisplayComponent
    : public juce::Component
    , public juce::Timer {
public:
    static constexpr int kNumPoints = 256; // samples drawn per frame trace
    static constexpr int kGhostFrames = 3; // traces drawn behind the active frame

    explicit WavetableDisplayComponent(WavetableOscillatorModule& moduleToDisplay)
        : module(moduleToDisplay) {
        setInterceptsMouseClicks(false, false);
        refreshWaveform();
        startTimerHz(15);
    }

    ~WavetableDisplayComponent() override { stopTimer(); }

    /** Quantised scan position — the display only needs to move in visible steps, and
        quantising keeps the repaint gate from firing on inaudible parameter jitter. */
    static int quantisePosition(float position, int steps = 200) {
        return (int)std::round(juce::jlimit(0.0f, 1.0f, position) * (float)steps);
    }

    void timerCallback() override {
        const int tablePos = quantisePosition(module.getScanPosition());
        const juce::String name = module.getWavetableName();
        const int frames = module.getNumFrames();
        // The drawn trace is warped (see getDisplayWaveformAt), so the warp mode and amount
        // are part of what makes it change — without them a Warp tweak would not redraw.
        const int warp = module.getWarpSignature();

        if (tablePos == lastPosition && name == lastName && frames == lastFrames && warp == lastWarp)
            return;

        lastPosition = tablePos;
        lastName = name;
        lastFrames = frames;
        lastWarp = warp;
        refreshWaveform();
        repaint();
    }

    void paint(juce::Graphics& g) override {
        using synth::theme::AppLookAndFeel;
        auto* lf = dynamic_cast<AppLookAndFeel*>(&getLookAndFeel());

        const juce::Colour bg = lf ? lf->getTheme().colors.bg1 : juce::Colour(0xff1a1a2e);
        const juce::Colour border = lf ? lf->getTheme().colors.border : juce::Colour(0xff2a2a3e);
        const juce::Colour accent = lf ? lf->getTheme().colors.accent : juce::Colour(0xff00b4d8);
        const juce::Colour mutedText = lf ? lf->getTheme().colors.textMuted : juce::Colour(0xff6a6a7e);

        const auto bounds = getLocalBounds().toFloat();
        g.setColour(bg);
        g.fillRoundedRectangle(bounds, 4.0f);
        g.setColour(border);
        g.drawRoundedRectangle(bounds.reduced(0.5f), 4.0f, 1.0f);

        // Zero line
        g.setColour(border.withAlpha(0.6f));
        g.drawHorizontalLine((int)bounds.getCentreY(), bounds.getX(), bounds.getRight());

        // Receding ghost traces, furthest first, then the active frame on top.
        for (int ghost = kGhostFrames; ghost >= 1; --ghost) {
            const float alpha = 0.10f + 0.06f * (float)(kGhostFrames - ghost);
            g.setColour(accent.withAlpha(alpha));
            g.strokePath(buildTrace(ghostWaveforms[(size_t)(ghost - 1)], bounds, (float)ghost),
                         juce::PathStrokeType(1.0f));
        }

        g.setColour(accent);
        g.strokePath(buildTrace(waveform, bounds, 0.0f), juce::PathStrokeType(1.6f));

        // Caption: table name + scan position within the stack
        if (lastFrames > 0) {
            const int frameIndex = (int)std::round(module.getScanPosition() * (float)(lastFrames - 1));
            g.setColour(mutedText);
            g.setFont(11.0f);
            g.drawText(lastName + "  " + juce::String(frameIndex + 1) + "/" + juce::String(lastFrames),
                       getLocalBounds().reduced(6, 3), juce::Justification::topLeft, true);
        }
    }

private:
    /** Builds a polyline for one trace. `depth` pushes the trace up and inwards so the
        ghost frames sit "behind" the active one. */
    static juce::Path buildTrace(const std::vector<float>& samples, juce::Rectangle<float> bounds, float depth) {
        juce::Path path;
        if (samples.size() < 2)
            return path;

        const float inset = 6.0f + depth * 5.0f;
        const float area = bounds.reduced(inset, 8.0f).getHeight();
        const float top = bounds.getY() + 8.0f - depth * 4.0f;
        const float left = bounds.getX() + inset;
        const float width = bounds.getWidth() - inset * 2.0f;
        const float centre = top + area * 0.5f;
        const float scale = area * 0.45f;

        for (size_t i = 0; i < samples.size(); ++i) {
            const float x = left + width * (float)i / (float)(samples.size() - 1);
            const float y = centre - juce::jlimit(-1.5f, 1.5f, samples[i]) * scale;
            if (i == 0)
                path.startNewSubPath(x, y);
            else
                path.lineTo(x, y);
        }
        return path;
    }

    void refreshWaveform() {
        module.getDisplayWaveform(waveform, kNumPoints);

        // Ghost traces sample slightly ahead in the stack so the scan direction is visible.
        const int frames = std::max(1, module.getNumFrames());
        const float step = 1.0f / (float)frames;
        for (int ghost = 0; ghost < kGhostFrames; ++ghost) {
            const float pos = juce::jlimit(0.0f, 1.0f, module.getScanPosition() + step * (float)(ghost + 1));
            module.getDisplayWaveformAt(ghostWaveforms[(size_t)ghost], kNumPoints, pos);
        }
    }

    WavetableOscillatorModule& module;
    std::vector<float> waveform;
    std::array<std::vector<float>, kGhostFrames> ghostWaveforms;

    int lastPosition = -1;
    int lastFrames = -1;
    int lastWarp = -1;
    juce::String lastName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplayComponent)
};
