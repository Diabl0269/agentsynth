#pragma once

#include "TimelineViewState.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {
class TransportService;
}

// TimelineRulerComponent — TL5-2: the bar/beat ruler strip at the top of the timeline panel's
// lanes region. Pure view: owns nothing. It references a shared synth::ui::TimelineViewState (the
// beat<->pixel mapping — zoom/scroll/snap) and an optional synth::TransportService (time
// signature + loop state for painting, and the target of click-to-seek / drag-to-loop). The
// TransportService pointer may be null (tests, or a build/flag state with no engine wired in yet):
// paint() then just shows an empty ruler and mouse interactions are no-ops.
//
// No timer, no animation of its own — repaint() is called only after an interaction changes
// something this component paints (view-state zoom/scroll, or after posting a transport command).
// The playhead itself is TL5-4.
namespace synth::ui {

class TimelineRulerComponent : public juce::Component {
public:
    explicit TimelineRulerComponent(TimelineViewState& viewState);

    void setTransport(synth::TransportService* transport) noexcept { transport_ = transport; }
    synth::TransportService* getTransport() const noexcept { return transport_; }

    void paint(juce::Graphics& g) override;

    // Click (no drag since mouseDown) = seek. Press-drag-release = set loop to the snapped
    // [min(anchor, current), max(anchor, current)] range. Cmd+click (no drag) = toggle looping
    // off, keeping the existing loop bounds. See TimelineRulerComponent.cpp for the throttle that
    // keeps drag updates from flooding TransportService's command FIFO.
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseUp(const juce::MouseEvent& e) override;

private:
    double snappedBeatAtX(double x) const noexcept;
    double currentBeatsPerBar() const noexcept;
    void postLoopIfChanged(const juce::MouseEvent& e);

    TimelineViewState& viewState_;
    synth::TransportService* transport_ = nullptr;

    // Drag-loop gesture state (message thread only; mouseDown/mouseDrag/mouseUp are always
    // dispatched from there).
    double dragAnchorBeat_ = 0.0;
    // Sentinel (never a valid snapped beat pair, since a beat under a ruler x >= 0 can't be
    // negative) meaning "nothing posted yet this gesture".
    double lastPostedLoopStart_ = -1.0;
    double lastPostedLoopEnd_ = -1.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRulerComponent)
};

} // namespace synth::ui
