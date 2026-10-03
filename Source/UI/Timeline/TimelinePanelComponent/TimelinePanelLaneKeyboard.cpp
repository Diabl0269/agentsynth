// TimelinePanelLaneKeyboard.cpp
//
// Lanes in the keyboard order of the track list: Up/Down walk track row -> its open lanes (each lane header, then its
// modulator rows) -> next track row, and back. Folded lanes are skipped. The track rows' own stepping (clamping, "+
// Track" at both ends) stays in TimelinePanelTrackHeaders.cpp; this file only adds the stops between two track rows.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory
// hold the rest of the class.

#include "TimelinePanelComponent.h"

#include <algorithm>

namespace synth::ui {

// Every stop of the column in on-screen order: a track row, then (only while its lanes are open) that track's lane
// headers and modulator rows. The Unassigned section's row is a stop like any track row.
std::vector<juce::Component*> TimelinePanelComponent::keyboardStops() const {
    std::vector<juce::Component*> stops;
    if (doc_ == nullptr)
        return stops;
    for (auto* header : trackHeaderList_.headers) {
        stops.push_back(header);
        if (const auto* track = doc_->getTrack(header->getTrackId()))
            for (auto* stop : automationLanes_.keyboardStopsFor(*track))
                stops.push_back(stop);
    }
    return stops;
}

void TimelinePanelComponent::wireLaneKeyboard() {
    automationLanes_.onFocusMoveRequested = [this](juce::Component& from, int direction) {
        stepFocusFromLaneStop(from, direction);
    };
    // Real focus arriving by Tab or a click on a combo's row keeps the model in step.
    automationLanes_.onKeyboardStopFocused = [this](juce::Component& stop) {
        keyboardStop_ = &stop;
        addTrackFocusRecorded_ = false;
    };
}

// Up/Down on a lane header or modulator row: the neighbouring stop. Stepping onto a track row selects that track
// exactly as its own arrows do (focused index, scroll, routing pane); stepping off the bottom lands on "+ Track" like
// the last track row does. The top is always a track row, so Up never falls off.
void TimelinePanelComponent::stepFocusFromLaneStop(juce::Component& from, int direction) {
    const auto stops = keyboardStops();
    const auto at = std::find(stops.begin(), stops.end(), &from);
    if (at == stops.end())
        return;
    const auto next = direction > 0 ? at + 1 : at - 1;
    if (next < stops.begin())
        return;
    if (next >= stops.end()) {
        keyboardStop_ = nullptr;
        addTrackFromTop_ = false;
        focusAddTrackButton();
        return;
    }
    if (auto* header = dynamic_cast<TimelineTrackHeaderComponent*>(*next)) {
        keyboardStop_ = nullptr;
        setFocusedTrack(header->getTrackId());
        header->grabKeyboardFocus();
        ensureTrackVisible(focusedTrackIndex_);
        return;
    }
    focusKeyboardStop(**next);
}

// The stop takes keyboard focus (a best-effort no-op without a native window) and the model follows: the owning track
// becomes the focused one, so the routing pane shows it and Escape from the clips returns to it.
void TimelinePanelComponent::focusKeyboardStop(juce::Component& stop) {
    addTrackFocusRecorded_ = false;
    keyboardStop_ = &stop;
    stop.grabKeyboardFocus();
    ensureStopVisible(stop);
    if (doc_ == nullptr)
        return;
    for (int i = 0; i < trackHeaderList_.headers.size(); ++i) {
        const auto* track = doc_->getTrack(trackHeaderList_.headers.getUnchecked(i)->getTrackId());
        if (track == nullptr)
            continue;
        const auto stops = automationLanes_.keyboardStopsFor(*track);
        if (std::find(stops.begin(), stops.end(), &stop) != stops.end()) {
            if (focusedTrackIndex_ != i) {
                focusedTrackIndex_ = i;
                refreshRoutingPane();
            }
            return;
        }
    }
}

// Scrolls the shared rows just far enough to show `stop`, from the stop's own bounds in the header column.
void TimelinePanelComponent::ensureStopVisible(const juce::Component& stop) {
    const int top = stop.getY();
    const int bottom = stop.getBottom();
    const int viewTop = (int)std::llround(viewState_.trackScrollY);
    const int viewHeight = trackHeaderViewport_.getMaximumVisibleHeight();
    if (top < viewTop)
        scrollTrackRows((double)(top - viewTop));
    else if (bottom > viewTop + viewHeight)
        scrollTrackRows((double)(bottom - (viewTop + viewHeight)));
}

} // namespace synth::ui
