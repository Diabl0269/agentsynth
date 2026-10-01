// TimelinePanelAddAutomation.cpp
//
// The panel's side of adding an automation lane from the timeline: the "+ Add automation..." row under a
// track's lanes and the track header's "Add automation..." menu entry both land in openAddAutomationPicker(),
// which shows the searchable list of that track's parameters and, on a pick, asks the host to create the lane
// on that track and shows it. TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling
// TimelinePanel*.cpp files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"

namespace synth::ui {

std::unique_ptr<ModMatrixPicker> TimelinePanelComponent::buildAddAutomationPickerFor(synth::TrackId track) {
    const auto* t = doc_ != nullptr ? doc_->getTrack(track) : nullptr;
    if (t == nullptr || trackHeaderHost_ == nullptr)
        return nullptr;
    const auto choices = collectAddAutomationChoices(*trackHeaderHost_, *doc_, track);
    // The pick arrives after the callout closes, by which time the panel may be gone.
    juce::Component::SafePointer<TimelinePanelComponent> safeThis(this);
    return buildAddAutomationPicker(choices, t->name, [safeThis, track](const auto& parameter) {
        if (auto* self = safeThis.getComponent())
            self->addAutomationLaneFromPicker(track, parameter);
    });
}

// The host owns the graph that knows the parameter's range and index hint; the lane lands on the track
// the person asked about, and is then shown exactly like a lane made from a knob.
void TimelinePanelComponent::addAutomationLaneFromPicker(synth::TrackId track,
                                                         const TrackHeaderHost::AutomatableParameter& parameter) {
    if (trackHeaderHost_ == nullptr)
        return;
    const auto lane = trackHeaderHost_->addAutomationLane(track, parameter);
    if (lane.isValid())
        showAutomationLane(lane);
}

void TimelinePanelComponent::openAddAutomationPicker(synth::TrackId track, juce::Component& anchor) {
    auto picker = buildAddAutomationPickerFor(track);
    if (picker == nullptr)
        return;
    if (addAutomationPickerHook_) {
        addAutomationPickerHook_(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), anchor.getScreenBounds(), nullptr);
}

AddAutomationRow* TimelinePanelComponent::addAutomationRowForTest(synth::TrackId track) const {
    return automationLanes_.addRowFor(track);
}

juce::Rectangle<int> TimelinePanelComponent::addAutomationRowBoundsForTest(synth::TrackId track) const {
    const auto* row = automationLanes_.addRowFor(track);
    if (row == nullptr)
        return {};
    return getLocalArea(row, row->getLocalBounds());
}

} // namespace synth::ui
