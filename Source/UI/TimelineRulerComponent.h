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

class TimelineRulerComponent : public juce::Component {
public:
    // Which interaction the pointer's height in the strip selects. Top half drives the loop
    // range, bottom half drives the playhead. Decided once from the mouseDown y and held for the
    // whole gesture — a drag is free to leave the band it started in.
    enum class Zone { Loop, Playhead };

    explicit TimelineRulerComponent(TimelineViewState& viewState);

    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    void paint(juce::Graphics& g) override;

    // Loop zone (top half): press-drag-release sets the loop to the snapped
    // [min(anchor, current), max(anchor, current)] range; a click with no drag does NOTHING, so a
    // stray click can't destroy an existing loop. Playhead zone (bottom half): mouseDown seeks
    // immediately and every mouseDrag keeps seeking, so the cursor follows the mouse. Cmd+click
    // toggles looping off from either zone. See TimelineRulerComponent.cpp for the throttle that
    // keeps both drag paths from flooding TransportService's command FIFO.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

    // Hover affordance only: sets the per-zone mouse cursor and paints a faint band over the
    // hovered half. Repaints only when the hovered zone actually changes.
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    // Which zone the pointer is over; nullopt when the pointer is not over the strip.
    std::optional<Zone> getHoveredZoneForTest() const noexcept { return hoveredZone_; }
    // The zone the in-flight (or most recent) gesture is locked to.
    Zone getGestureZoneForTest() const noexcept { return gestureZone_; }
    // How many locateBeat() calls the scrub path has actually posted — proves the dedupe throttle.
    int getSeekPostCountForTest() const noexcept { return seekPostCount_; }

private:
    Zone zoneAtY(float y) const noexcept;
    double snappedBeatAtX(double x) const noexcept;
    double currentBeatsPerBar() const noexcept;
    void postLoopIfChanged(const juce::MouseEvent& e);
    void postSeekIfChanged(const juce::MouseEvent& e);
    void setHoveredZone(std::optional<Zone> zone);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;

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
