#pragma once

#include "Modules/WavetableOscillatorModule/WavetableOscillatorModule.h"
#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/ReducedMotion.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/Theme.h"
#include <array>
#include <cmath>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/**
    Draws the wavetable frame the oscillator plays now, with a few frames from either side
    receding behind it so the stack reads as three-dimensional.

    It shows the live (modulated) Position and Warp, knob plus cable while a note sounds and the
    knob alone at rest (WavetableOscillatorModule::getModulatedScanPosition), so the frame and
    the caption sweep with an LFO on Position. Between the audio thread's values the drawn
    position glides (kGlideMs under full Animations, a jump under Reduce Motion or Off).

    Repaints are gated: the timer only calls repaint() when the table, the frame count, the
    drawn position or the warp actually changed (docs/layout/rendering.md). It ticks at 15 Hz
    and at 30 Hz only while the drawn position is moving.
*/
class WavetableDisplayComponent
    : public juce::Component
    , public juce::SettableTooltipClient
    , public juce::Timer
    , public synth::testmode::StateSource {
public:
    static constexpr int kNumPoints = 256;   // samples drawn per frame trace
    static constexpr int kGhostFrames = 3;   // traces drawn behind the active frame
    static constexpr int kRestHz = 15;       // timer rate while the drawn position rests
    static constexpr int kMovingHz = 30;     // timer rate while it moves
    static constexpr double kGlideMs = 60.0; // time constant of the glide toward the live value

    explicit WavetableDisplayComponent(WavetableOscillatorModule& moduleToDisplay)
        : module(moduleToDisplay) {
        setInterceptsMouseClicks(false, false);
        setComponentID("wavetableDisplay");
        setTitle("Wavetable display");
        setDescription("The wavetable frame the oscillator plays now");
        setTooltip("Wavetable display: the frame the oscillator plays now, following any cable on Position or Warp");
        shownPosition = lastTarget = module.getModulatedScanPosition();
        refreshWaveform();
        startTimerHz(kRestHz);
    }

    ~WavetableDisplayComponent() override { stopTimer(); }

    /** Quantised scan position: the display only moves in visible steps, and quantising keeps the
        repaint gate from firing on inaudible jitter. */
    static int quantisePosition(float position, int steps = 200) {
        return (int)std::round(juce::jlimit(0.0f, 1.0f, position) * (float)steps);
    }

    /** One glide step of `elapsedMs` from `shown` toward `target` with time constant `glideMs`; 0 jumps. */
    static float glideToward(float shown, float target, double elapsedMs, double glideMs) {
        if (glideMs <= 0.0 || elapsedMs <= 0.0)
            return glideMs <= 0.0 ? target : shown;
        const float k = 1.0f - (float)std::exp(-elapsedMs / glideMs);
        const float next = shown + (target - shown) * k;
        return quantisePosition(next) == quantisePosition(target) ? target : next;
    }

    void timerCallback() override { tick(juce::Time::getMillisecondCounterHiRes()); }

    /** Moves the drawn position toward the live one for the time since the last tick, then repaints
        only if what is drawn changed. */
    void tick(double nowMs) {
        const float target = module.getModulatedScanPosition();
        const double elapsed = lastTickMs < 0.0 ? 0.0 : nowMs - lastTickMs;
        lastTickMs = nowMs;
        shownPosition = glideToward(shownPosition, target, elapsed, synth::ui::motionMs(kGlideMs, 0.0));
        shownWarpAmount = module.getModulatedWarpAmount();

        // Moving while the drawn position trails the live one or the live one itself moved since the last tick.
        const bool moving = quantisePosition(shownPosition) != quantisePosition(target) ||
                            quantisePosition(target) != quantisePosition(lastTarget);
        lastTarget = target;
        const int hz = moving ? kMovingHz : kRestHz;
        if (getTimerInterval() != 1000 / hz)
            startTimerHz(hz);

        const int tablePos = quantisePosition(shownPosition);
        const juce::String name = module.getWavetableName();
        const int frames = module.getNumFrames();
        // The drawn trace is warped, so the warp mode and the live amount are part of what makes it change.
        const int warp = module.getWarpSignature(shownWarpAmount);

        if (tablePos == lastPosition && name == lastName && frames == lastFrames && warp == lastWarp)
            return;

        lastPosition = tablePos;
        lastName = name;
        lastFrames = frames;
        lastWarp = warp;
        refreshWaveform();
        repaint();
    }

    /** The position drawn now, 0..1 (the live value, glided). */
    float getShownPosition() const { return shownPosition; }

    /** The frame the caption names, 1-based; 0 before the first tick or without a table. */
    int getShownFrame() const {
        return lastFrames > 0 ? (int)std::round(shownPosition * (float)(lastFrames - 1)) + 1 : 0;
    }

    juce::String getCaption() const {
        return lastFrames > 0 ? lastName + "  " + juce::String(getShownFrame()) + "/" + juce::String(lastFrames)
                              : juce::String();
    }

    void addTestModeState(juce::DynamicObject& into) const override {
        into.setProperty("shownPosition", shownPosition);
        into.setProperty("livePosition", module.getModulatedScanPosition());
        into.setProperty("knobPosition", module.getScanPosition());
        into.setProperty("offKnob", module.getModulatedScanPosition() != module.getScanPosition());
        into.setProperty("shownFrame", getShownFrame());
        into.setProperty("caption", getCaption());
        into.setProperty("timerHz", getTimerInterval() > 0 ? 1000 / getTimerInterval() : 0);
    }

    void paint(juce::Graphics& g) override {
        const auto& colors = synth::theme::themeOf(*this).colors;
        const juce::Colour bg = colors.bg1;
        const juce::Colour border = colors.border;
        const juce::Colour accent = colors.accent;
        const juce::Colour mutedText = colors.textMuted;

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

        // Caption: table name + the drawn frame within the stack
        if (lastFrames > 0) {
            g.setColour(mutedText);
            g.setFont(11.0f);
            g.drawText(getCaption(), getLocalBounds().reduced(6, 3), juce::Justification::topLeft, true);
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
        module.getDisplayWaveformAt(waveform, kNumPoints, shownPosition, shownWarpAmount);

        // Ghost traces sample slightly ahead in the stack so the scan direction is visible.
        const int frames = std::max(1, module.getNumFrames());
        const float step = 1.0f / (float)frames;
        for (int ghost = 0; ghost < kGhostFrames; ++ghost) {
            const float pos = juce::jlimit(0.0f, 1.0f, shownPosition + step * (float)(ghost + 1));
            module.getDisplayWaveformAt(ghostWaveforms[(size_t)ghost], kNumPoints, pos, shownWarpAmount);
        }
    }

    WavetableOscillatorModule& module;
    std::vector<float> waveform;
    std::array<std::vector<float>, kGhostFrames> ghostWaveforms;

    float shownPosition = 0.0f;
    float shownWarpAmount = module.getModulatedWarpAmount();
    double lastTickMs = -1.0;
    float lastTarget = shownPosition;
    int lastPosition = -1;
    int lastFrames = -1;
    int lastWarp = -1;
    juce::String lastName;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WavetableDisplayComponent)
};
