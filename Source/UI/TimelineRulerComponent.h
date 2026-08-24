#pragma once

#include "TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <optional>

namespace synth {
class TransportService;
}

// The bar/beat ruler strip at the top of the timeline panel's lanes region. Pure view: owns
// nothing. It references a shared synth::ui::TimelineViewState (the beat<->pixel mapping —
// zoom/scroll/snap) and an optional synth::TransportService (time signature + loop state for
// painting, and the target of drag-to-scrub / drag-to-loop). The TransportService pointer may be
// null (tests, or a build/flag state with no engine wired in yet): paint() then just shows an
// empty ruler and mouse interactions are no-ops.
//
// The strip is split horizontally into two interaction zones — top half = loop, bottom half =
// playhead — so both gestures are reachable without a modifier. See the Zone enum below.
//
// No timer, no animation of its own — repaint() is called after an interaction changes something
// this component paints (view-state zoom/scroll, the hovered zone, or after posting a transport
// command), or by TimelinePanelComponent::updateFromTransport's 10 Hz diff when the time signature
// / loop range changed from somewhere else. The moving position line is a separate topmost overlay
// drawn over this strip and the lanes below it — see TimelinePlayheadOverlay.
namespace synth::ui {

// ---- Adaptive ruler density (pure; paint() and the tests below share it) ----
//
// Cubase's ruler shows three bands as you zoom in, and the SNAP division has no say in any of them
// — the ruler is a bars/beats reference, not a picture of the current grid (a 1/16 snap on a
// zoomed-out arrangement would otherwise turn the strip into a grey smear). The bands:
//   1. far out  — bar lines + bar numbers only (bar numbers themselves thin out by powers of two,
//                 see paint()'s labelEveryNBars);
//   2. mid      — short beat ticks appear between the bar lines;
//   3. close in — those beats also get a small dim "bar.beat" label ("80.2", "80.3").
//
// The two thresholds below are the band edges, in pixels per BEAT (a beat is always a quarter note
// here). Deterministic and font-independent — the same reason the bar-label stride is computed from
// a constant rather than from measured text: the guard tests must mean the same thing on every
// platform.
constexpr double kMinBeatTickSpacingPx = 8.0;   // under this, beat ticks are noise beside the bar lines
constexpr double kMinBeatLabelSpacingPx = 48.0; // "80.2" needs this much room before it earns its place

struct RulerTickPlan {
    bool drawBeatTicks = false;
    bool drawBeatLabels = false;
};

/** What the ruler draws BETWEEN bar lines at this zoom. `beatsPerBar` matters only for the
 *  degenerate one-beat-or-shorter bar: there are then no non-bar beats to mark at all, and drawing
 *  a "tick" on top of every bar line would just thicken it. Labels imply ticks by construction —
 *  a label with no tick to sit against would float. */
inline RulerTickPlan rulerTickPlanFor(double pixelsPerBeat, double beatsPerBar) noexcept {
    RulerTickPlan plan;
    if (!(pixelsPerBeat > 0.0) || !(beatsPerBar > 1.0))
        return plan;
    plan.drawBeatTicks = pixelsPerBeat >= kMinBeatTickSpacingPx;
    plan.drawBeatLabels = plan.drawBeatTicks && pixelsPerBeat >= kMinBeatLabelSpacingPx;
    return plan;
}

class TimelineRulerComponent : public juce::Component {
public:
    // Which interaction the pointer's height in the strip selects. Top half drives the loop
    // range, bottom half drives the playhead. Decided once from the mouseDown y and held for the
    // whole gesture — a drag is free to leave the band it started in.
    enum class Zone { Loop, Playhead };

    // How the loop brace paints. A RANGE exists whenever loopEnd > loopStart, independently of
    // whether looping is armed: disarming dims the brace instead of hiding it, so the locators stay
    // visible (and clickable — see mouseUp) once set.
    enum class BraceState { None, Inactive, Active };

    explicit TimelineRulerComponent(TimelineViewState& viewState);

    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    // While the piano roll is open it maps beats to x through its OWN zoom/scroll, so this strip
    // would otherwise label bars that have nothing to do with what is on screen below it.
    // TimelinePanelComponent hands the roll's view state here (plus the roll's keys-gutter width
    // as a pixel offset, since the roll's grid starts that far right of this strip's x == 0) on
    // openPianoRoll(), and clears it (nullptr) on close. Everything — labels, ticks, the loop
    // brace, drag-to-loop and drag-to-scrub — follows the override, so the ruler stays a truthful,
    // interactive ruler over the roll. The SNAP division still comes from the shared view state
    // (there is only one snap setting).
    void setMappingOverride(const TimelineViewState* view, int xOffsetPx) noexcept {
        overrideView_ = view;
        overrideOffsetPx_ = xOffsetPx;
        repaint();
    }
    bool hasMappingOverrideForTest() const noexcept { return overrideView_ != nullptr; }
    // The offset an installed override is currently using — what a test asserts against directly
    // rather than inferring it from painted tick positions (0 with no override installed, though
    // hasMappingOverrideForTest() is what actually gates whether that 0 means anything).
    int getMappingOverrideOffsetForTest() const noexcept { return overrideOffsetPx_; }

    void paint(juce::Graphics& g) override;

    // Loop zone (top half): press-drag-release sets the loop to the snapped
    // [min(anchor, current), max(anchor, current)] range; a click with no drag does NOTHING unless
    // it lands on a DIMMED brace, which re-arms that range (the inverse of the Cmd+click that
    // disarmed it) — a stray click anywhere else in this half still can't destroy an existing loop.
    // Playhead zone (bottom half): mouseDown seeks immediately and every mouseDrag keeps seeking, so
    // the cursor follows the mouse. Cmd+click toggles looping off from either zone. See
    // TimelineRulerComponent.cpp for the throttle that keeps both drag paths from flooding
    // TransportService's command FIFO.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Hover affordance only: sets the per-zone mouse cursor and paints a faint band over the
    // hovered half. Repaints only when the hovered zone actually changes.
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Pure: what paint() draws for this loop trio. The ONE place the rule lives — paint() and the
    // test seam below both call it, so a drawn brace and an asserted state can't diverge.
    static BraceState braceStateFor(bool looping, double loopStartBeat, double loopEndBeat) noexcept;
    // The colour each state paints in. Inactive is deliberately the muted TEXT colour rather than a
    // faded accent: the hover band is already accent at 10%, so a dimmed accent would still read as
    // lit over it.
    static juce::Colour braceColourFor(BraceState state, juce::Colour accent, juce::Colour textMuted) noexcept;
    // The brace state for the CURRENT transport (None with no transport at all).
    BraceState getBraceStateForTest() const noexcept;

    // Which zone the pointer is over; nullopt when the pointer is not over the strip.
    std::optional<Zone> getHoveredZoneForTest() const noexcept { return hoveredZone_; }
    // The zone the in-flight (or most recent) gesture is locked to.
    Zone getGestureZoneForTest() const noexcept { return gestureZone_; }
    // How many locateBeat() calls the scrub path has actually posted — proves the dedupe throttle.
    int getSeekPostCountForTest() const noexcept { return seekPostCount_; }

private:
    Zone zoneAtY(float y) const noexcept;
    // The beat<->x mapping every paint/gesture site goes through: the shared view state normally,
    // the override (offset by overrideOffsetPx_) while one is installed.
    double mapBeatToX(double beat) const noexcept;
    double mapXToBeat(double x) const noexcept;
    double mapPixelsPerBeat() const noexcept;
    double mapFirstVisibleBeat() const noexcept;
    double snappedBeatAtX(double x) const noexcept;
    double currentBeatsPerBar() const noexcept;
    void postLoopIfChanged(const juce::MouseEvent& e);
    void postSeekIfChanged(const juce::MouseEvent& e);
    // Re-arms an existing-but-disabled loop when a no-drag loop-zone click lands inside its span.
    // Inert in every other case (looping already on, no range, click outside the brace).
    void reArmLoopIfClickOnInactiveBrace(const juce::MouseEvent& e);
    void setHoveredZone(std::optional<Zone> zone);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;

    // Non-owning; set/cleared by the panel around the piano roll's open/close (see
    // setMappingOverride). The pointee outlives the override window — both live on the panel.
    const TimelineViewState* overrideView_ = nullptr;
    int overrideOffsetPx_ = 0;

    // Gesture state (message thread only; mouseDown/mouseDrag/mouseUp are always dispatched from
    // there). gestureZone_ is latched in mouseDown.
    Zone gestureZone_ = Zone::Playhead;
    double dragAnchorBeat_ = 0.0;
    // Sentinel (never a valid posted beat, since every posted beat is clamped to >= 0) meaning
    // "nothing posted yet this gesture".
    double lastPostedLoopStart_ = -1.0;
    double lastPostedLoopEnd_ = -1.0;
    double lastPostedSeekBeat_ = -1.0;
    int seekPostCount_ = 0;

    std::optional<Zone> hoveredZone_;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRulerComponent)
};

} // namespace synth::ui
