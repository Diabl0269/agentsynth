// TimelinePanelTrackLanes.cpp
//
// Concern: the panel's side of per-track automation lanes (docs/timeline/track-automation.md) --
// wiring the TrackAutomationLanes collaborator, revealing a lane wherever it lives, the toolbar's
// global-strip toggle and the persisted "automation follows clips" preference.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files
// in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

namespace synth::ui {

namespace {
constexpr const char* kAutomationFollowsClipsPropertyKey = "timelineAutomationFollowsClips";
} // namespace

// A constructor step (the constructor sits near the function-size cap). The editor layer is added
// here, i.e. AFTER the clip lanes and the piano roll and BEFORE the playhead (added last by the
// constructor), so z-order reads clips -> lane rows -> playhead.
void TimelinePanelComponent::setUpTrackAutomationLanes() {
    addAndMakeVisible(trackLanes_.getEditorLayer());
    trackLanes_.addToolbarTo(*this);

    TrackAutomationLanes::Callbacks callbacks;
    callbacks.relayout = [this] {
        layoutTrackHeaders();
        clipLaneArea_.repaint();
        repaint(gridLanesBounds_);
    };
    callbacks.rowLayout = [this] { return &clipLaneArea_.getRowLayout(); };
    callbacks.ensureContentVisible = [this](int top, int bottom) { ensureContentRangeVisible(top, bottom); };
    callbacks.toggleGlobalStrip = [this] { toggleGlobalAutomationStrip(); };
    callbacks.toggleFollowsClips = [this] { setAutomationFollowsClips(!isAutomationFollowsClips()); };
    trackLanes_.setCallbacks(std::move(callbacks));
    refreshAutomationToolbar();
}

// THE reveal seam every "show me this lane" path ends in (MainComponent::automateParameter,
// addPluginAutomationLane, the strip's "Add lane..." entries): a lane stored on a Midi/Audio track
// is shown as a row under that track -- expanded, scrolled into view and focused -- and a lane on an
// Automation-kind track (a global one) opens the bottom strip on it, exactly as before track lanes.
void TimelinePanelComponent::revealAutomationLane(synth::LaneId id) {
    if (doc_ == nullptr || doc_->getLane(id) == nullptr)
        return;
    if (!trackLanes_.revealLane(id))
        showAutomationLane(id);
}

int TimelinePanelComponent::getGlobalAutomationLaneCount() const {
    int count = 0;
    if (doc_ != nullptr)
        for (const auto& track : doc_->getTracks())
            if (track.kind == synth::TrackKind::Automation)
                count += (int)track.lanes.size();
    return count;
}

// Open -> close. Closed -> the lane the strip last showed when it is still a global lane, else the
// first global lane; with no global lane at all the strip opens EMPTY on its picker (the "Add
// lane..." entries), so the button is also the way to create the first global lane.
void TimelinePanelComponent::toggleGlobalAutomationStrip() {
    if (automationStripVisible_) {
        closeAutomationStrip();
        return;
    }
    if (doc_ == nullptr)
        return;

    auto isGlobal = [this](synth::LaneId lane) {
        const auto* track = doc_->getTrackForLane(lane);
        return track != nullptr && track->kind == synth::TrackKind::Automation;
    };
    if (isGlobal(selectedAutomationLane_)) {
        showAutomationLane(selectedAutomationLane_);
        return;
    }
    for (const auto& track : doc_->getTracks())
        if (track.kind == synth::TrackKind::Automation && !track.lanes.empty()) {
            showAutomationLane(track.lanes.front().id);
            return;
        }

    selectedAutomationLane_ = {};
    automationStripVisible_ = true;
    automationEditor_.setTimelineDoc(doc_);
    automationEditor_.setActiveLane({});
    syncAutomationLaneCombo();
    syncAutomationRecordModeCombo();
    resized();
    repaint();
    refreshAutomationToolbar();
    if (laneCombo_.isShowing() && laneCombo_.getNumItems() > 0)
        laneCombo_.showPopup();
}

void TimelinePanelComponent::setAutomationFollowsClips(bool follows) {
    clipLaneArea_.setAutomationFollowsClips(follows);
    refreshAutomationToolbar();
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    appProperties_->getUserSettings()->setValue(kAutomationFollowsClipsPropertyKey, follows);
    appProperties_->saveIfNeeded();
}

// Restored the same way the snap and follow-playhead switches are (setApplicationProperties).
void TimelinePanelComponent::restoreAutomationFollowsClipsPref() {
    if (appProperties_ == nullptr || appProperties_->getUserSettings() == nullptr)
        return;
    clipLaneArea_.setAutomationFollowsClips(
        appProperties_->getUserSettings()->getBoolValue(kAutomationFollowsClipsPropertyKey, false));
    refreshAutomationToolbar();
}

void TimelinePanelComponent::refreshAutomationToolbar() {
    trackLanes_.refreshToolbar(getGlobalAutomationLaneCount(), automationStripVisible_, isAutomationFollowsClips());
}

} // namespace synth::ui
