// TimelinePanelAutomation.cpp
//
// The panel's side of the automation lanes that fold out under their tracks: wiring the
// TimelineAutomationLanes collaborator in, feeding its rows into the shared row layout, the fold
// arrow, showAutomationLane() and the lane choices the mixer's "Automate" entries use.
// TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp files
// in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

namespace synth::ui {

// The editors' container goes in above the clip lanes and the piano roll and below the playhead,
// so the line still runs through the lanes. A fold toggle relayouts like a doc change does.
void TimelinePanelComponent::initAutomationLanes() {
    addAndMakeVisible(automationLanes_.getBodies());
    automationLanes_.onLayoutChanged = [this] {
        pushAutomationGeometry();
        layoutAutomationRows();
    };
    automationLanes_.onLaneFocused = [this](synth::LaneId lane) { selectedAutomationLane_ = lane; };
    wireLaneKeyboard();
    automationLanes_.onAddAutomationRequested = [this](synth::TrackId track, juce::Component& row) {
        openAddAutomationPicker(track, row);
    };
}

void TimelinePanelComponent::syncAutomationLanes() {
    automationLanes_.sync();
    pushAutomationGeometry();
    layoutAutomationRows();
}

// The clip lanes own THE row layout every other surface reads, so the lanes' rows enter it there.
void TimelinePanelComponent::pushAutomationGeometry() {
    clipLaneArea_.setTrackExtraHeights(automationLanes_.extraHeights());
    clipLaneArea_.setTrackRowHeightOverrides(automationLanes_.rowHeightOverrides());
}

// Folding a long track shrinks the content, so the scroll is clamped back into range here rather
// than leaving the view parked past the end.
void TimelinePanelComponent::layoutAutomationRows() {
    for (auto* header : trackHeaderList_.headers) {
        header->setHiddenLaneCount(automationLanes_.hiddenLaneCount(header->getTrackId()));
        header->setAutomationExpanded(automationLanes_.isExpanded(header->getTrackId()));
    }
    layoutTrackHeaders();
    viewState_.scrollTracksPx(0.0, maxTrackScrollPx());
    syncTrackScroll();
}

void TimelinePanelComponent::placeLaneBodies() { automationLanes_.placeBodies(rowLayout()); }

void TimelinePanelComponent::toggleAutomationForTrack(synth::TrackId trackId) {
    setTrackAutomationExpanded(trackId, !automationLanes_.isExpanded(trackId));
}

void TimelinePanelComponent::setTrackAutomationExpanded(synth::TrackId track, bool expanded) {
    automationLanes_.setExpanded(track, expanded);
}

// Every way of automating a parameter ends here (the knob's "Automate", the mixer's lane choices),
// so the lane the user just asked for is always on screen and ready for the keyboard: its track
// opens, the view scrolls to the row, and its editor takes focus.
void TimelinePanelComponent::showAutomationLane(synth::LaneId id) {
    const auto* track = doc_ != nullptr ? doc_->getTrackForLane(id) : nullptr;
    if (track == nullptr)
        return;
    selectedAutomationLane_ = id;
    automationLanes_.setExpanded(track->id, true);
    syncAutomationLanes();
    if (automationLanes_.isAmountLane(id))
        return; // shown as its modulator's amount band: the track is open, and there is no lane row to scroll to

    const auto row = automationLanes_.laneRowContentBounds(id, rowLayout());
    const int viewTop = (int)std::llround(viewState_.trackScrollY);
    const int viewHeight = gridLanesBounds_.getHeight();
    if (row.getY() < viewTop)
        scrollTrackRows((double)(row.getY() - viewTop));
    else if (row.getBottom() > viewTop + viewHeight)
        scrollTrackRows((double)(row.getBottom() - (viewTop + viewHeight)));

    if (auto* editor = automationLanes_.editorFor(id))
        editor->grabKeyboardFocus(); // best-effort: a no-op without a native window
}

// Existing lanes first (in track order then lane order), then "Add lane..." entries -- index i is
// choice i + 1, the same convention TimelineTrackHeaderComponent::collectBindingOptions() uses.
// Re-run at click time on purpose: lanes are document data mutated only on the message thread.
std::vector<TimelinePanelComponent::AutomationLaneOption> TimelinePanelComponent::collectAutomationLaneOptions() const {
    std::vector<AutomationLaneOption> options;
    if (doc_ == nullptr)
        return options;

    for (const auto& track : doc_->getTracks()) {
        for (const auto& lane : track.lanes) {
            if (automationLanes_.isAmountLane(lane.id))
                continue; // a modulator's amount lane is not a lane row to pick
            const auto labels = laneLabelsFor(lane, trackHeaderHost_);
            options.push_back(
                {lane.id, labels.module + juce::String::fromUTF8(" \xC2\xB7 ") + labels.parameter, false, {}});
        }
    }

    if (trackHeaderHost_ != nullptr) {
        for (auto& addOption : trackHeaderHost_->getAvailablePluginLaneOptions()) {
            AutomationLaneOption option;
            option.label = "Add: " + addOption.label;
            option.isAddEntry = true;
            option.addOption = addOption;
            options.push_back(std::move(option));
        }
    }
    return options;
}

void TimelinePanelComponent::applyAutomationLaneMenuChoice(int selectedId) {
    const auto options = collectAutomationLaneOptions();
    if (selectedId < 1 || selectedId > (int)options.size())
        return;
    const auto& chosen = options[(size_t)(selectedId - 1)];
    if (!chosen.isAddEntry) {
        showAutomationLane(chosen.id);
        return;
    }
    // Find-or-create through the host (it owns the graph that says which track the lane belongs
    // on), then shown exactly like an existing lane.
    if (trackHeaderHost_ == nullptr)
        return;
    const auto laneId = trackHeaderHost_->addPluginAutomationLane(chosen.addOption);
    if (laneId.isValid())
        showAutomationLane(laneId);
}

bool TimelinePanelComponent::isTrackAutomationExpandedForTest(synth::TrackId track) const {
    return automationLanes_.isExpanded(track);
}

juce::Rectangle<int> TimelinePanelComponent::laneRowBoundsForTest(synth::LaneId lane) const {
    const auto row = automationLanes_.laneRowContentBounds(lane, rowLayout());
    if (row.isEmpty())
        return {};
    return row.translated(gridLanesBounds_.getX(),
                          gridLanesBounds_.getY() - (int)std::llround(viewState_.trackScrollY));
}

AutomationLaneEditor* TimelinePanelComponent::laneEditorForTest(synth::LaneId lane) const {
    return automationLanes_.editorFor(lane);
}

AutomationLaneHeaderComponent* TimelinePanelComponent::laneHeaderForTest(synth::LaneId lane) const {
    return automationLanes_.headerFor(lane);
}

// The panel has no graph: the host calls this after every graph change, and the lanes ask it back for
// each open lane's routings. A changed set of rows relayouts through onLayoutChanged.
void TimelinePanelComponent::refreshModulators() { automationLanes_.refreshModulators(); }

ModulatorRow* TimelinePanelComponent::modulatorRowForTest(synth::LaneId lane, int index) const {
    return automationLanes_.modulatorRowFor(lane, index);
}

ModulatorBand* TimelinePanelComponent::modulatorBandForTest(synth::LaneId lane, int index) const {
    return automationLanes_.modulatorBandFor(lane, index);
}

juce::Rectangle<int> TimelinePanelComponent::modulatorRowBoundsForTest(synth::LaneId lane, int index) const {
    const auto row = automationLanes_.modulatorRowContentBounds(lane, index, rowLayout());
    if (row.isEmpty())
        return {};
    return row.translated(gridLanesBounds_.getX(),
                          gridLanesBounds_.getY() - (int)std::llround(viewState_.trackScrollY));
}

} // namespace synth::ui
