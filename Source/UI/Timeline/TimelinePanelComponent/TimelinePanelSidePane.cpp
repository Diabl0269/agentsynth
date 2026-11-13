// TimelinePanelSidePane.cpp
//
// The Timeline tab's left side pane (docs/layout/side-pane.md): its wiring, layout and open/close, and the routing pane
// that follows the selected track (docs/timeline/tracks.md#routing-from-the-side-pane). TimelinePanelComponent is
// declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

namespace synth::ui {

// The pane is a child of this panel (so it travels into a detached window), holds the routing view, and starts CLOSED:
// unlike the Mixer's zones list it is opt-in. setApplicationProperties() then loads the remembered open state and width
// under the "timeline" key, again with a closed default.
void TimelinePanelComponent::initSidePane() {
    addChildComponent(sidePane_);
    addAndMakeVisible(sidePaneButton_);
    sidePaneButton_.bind(&sidePane_);
    sidePane_.setContent(&routingPane_);
    sidePane_.setOpen(false);
    sidePane_.onOccupiedWidthChanged = [this] { resized(); };
    // Closed panes are not refreshed while shut, so opening one re-reads it.
    sidePane_.addChangeListener(this);
}

// The pane takes the left of the body (under the transport strip, where its button sits); the header column, ruler and
// lanes share what is left.
void TimelinePanelComponent::layoutSidePane(juce::Rectangle<int>& body) {
    sidePane_.setBounds(body.removeFromLeft(sidePane_.getOccupiedWidth()));
    sidePaneButton_.setBounds(transportBarBounds_.withWidth(SidePaneToggleButton::kWidth));
}

bool TimelinePanelComponent::toggleSidePane(bool forceOpen) {
    if (!sidePane_.hasContent())
        return false;
    if (forceOpen)
        sidePane_.setOpen(true);
    else
        sidePane_.toggle();
    return true;
}

synth::TrackId TimelinePanelComponent::getSelectedTrackId() const {
    return juce::isPositiveAndBelow(focusedTrackIndex_, trackHeaderList_.headers.size())
               ? trackHeaderList_.headers.getUnchecked(focusedTrackIndex_)->getTrackId()
               : synth::TrackId();
}

// A view: nothing here edits the document, and a closed pane costs one early return.
void TimelinePanelComponent::refreshRoutingPane() {
    if (!sidePane_.isOpen() && !sidePane_.isAnimating())
        return;
    routingPane_.setTrack(getSelectedTrackId());
}

void TimelinePanelComponent::tickRoutingPane() {
    if (sidePane_.isOpen())
        routingPane_.tickMeter();
}

} // namespace synth::ui
