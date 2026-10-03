#pragma once

#include <functional>
#include <juce_graphics/juce_graphics.h>
#include <memory>
#include <optional>

namespace juce {
class Component;
}

// AutomatedMarker.h (docs/layout/module-card.md#automated-marker): the small "this parameter has an automation
// lane" glyph painted at the TOP-LEFT of a knob or fader -- a short slanted line with a dot at each end. It mirrors
// the violet MIDI-mapped dot, which sits top-right (synth::ui::midilearn::paintMidiMappedDot). It is only a marker:
// no click, no focus. The owner (module card, mixer column) caches one AutomatedMarkerFade per control, reads
// "is it automated" from its existing poll, and paints the glyph with the fade's level.
namespace synth::ui {

/** Pure: the box the glyph occupies for a control whose cell is `controlBounds` -- the cell's top-left corner. Never
 *  overlaps the MIDI dot (the cell's top-right, midilearn::midiMappedDotRect) for a cell at least kMinCellWidth wide.
 */
juce::Rectangle<int> automatedMarkerRect(juce::Rectangle<int> controlBounds);
constexpr int kAutomatedMarkerWidth = 8;
constexpr int kAutomatedMarkerHeight = 6;
/** Narrowest cell for which the marker and the MIDI dot (6 px) cannot touch. */
constexpr int kAutomatedMarkerMinCellWidth = kAutomatedMarkerWidth + 6;
/** Opacity of the glyph at full fade: the theme's textPrimary at 70 percent. */
constexpr float kAutomatedMarkerAlpha = 0.7f;

/** Paints the glyph at the top-left of `controlBounds` in `colour` (its alpha is the final opacity). */
void paintAutomatedMarker(juce::Graphics& g, juce::Rectangle<int> controlBounds, juce::Colour colour);

/** The glyph colour for a theme's `textPrimary` at fade `level` in [0, 1]. */
juce::Colour automatedMarkerColour(juce::Colour textPrimary, float level);

/** One control's marker fade: 160 ms ease-out in, 110 ms ease-in out (80 ms plain fade under reduced motion). A
 *  retarget mid-fade continues from the current level. Pure state; the owner supplies the clock. */
class AutomatedMarkerFade {
public:
    static constexpr double kInMs = 160.0;
    static constexpr double kOutMs = 110.0;
    static constexpr double kReducedMs = 80.0;

    /** Moves towards shown (`on`) or hidden; a no-op when already heading there. */
    void setAutomated(bool on, double nowMs, bool reducedMotion);
    bool isAutomated() const noexcept { return target_; }
    float level(double nowMs) const noexcept;
    /** True when no fade is running (level is exactly 0 or 1). */
    bool isSettled(double nowMs) const noexcept;

    /** Milliseconds clock every owner uses; a test can pin it. */
    static double nowMs();
    static void setClockForTest(std::optional<double> ms);

private:
    bool target_ = false;
    float from_ = 0.0f;
    double startMs_ = 0.0;
    double durationMs_ = 0.0;
};

/** What a control carries for the marker besides its fade, for owners that do not compose tooltips themselves (the
 *  mixer): while automated its tooltip gets an "Automated: <name>" line and its screen-reader description says
 *  "Automated"; both return to what they were when the lane goes. */
struct AutomatedControlState {
    AutomatedMarkerFade fade;
    /** Moves towards `automated`; returns whether that changed anything. */
    bool update(juce::Component& control, bool automated, const juce::String& name, double nowMs, bool reducedMotion);
};

/** Frame source for an owner's marker fades: a time-bounded driver that calls `onFrame` until the longest fade has
 *  run, then once more. Created on first use, so an owner that never has an automated control pays nothing; nothing
 *  runs while no fade is in flight. */
class AutomatedMarkerTicker {
public:
    explicit AutomatedMarkerTicker(juce::Component& owner);
    ~AutomatedMarkerTicker();
    void run(std::function<void()> onFrame);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace synth::ui
