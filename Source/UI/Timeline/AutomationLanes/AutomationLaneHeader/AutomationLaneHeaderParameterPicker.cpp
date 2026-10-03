// Concern: the automation lane header's parameter pickers -- "Change parameter..." (point this lane at another
// parameter, keeping its curve) and "Duplicate" (a copy of this lane, below it, on a parameter picked first). Both
// reuse the "+ Add automation..." picker, which already leaves out every parameter that has a lane, so a lane can
// never end up automating the same parameter twice. Every edit is ONE undo step through AutomationLaneActions.
#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

namespace synth::ui {

// Both items need a host (it knows each parameter's range) and at least one parameter without a lane.
bool AutomationLaneHeaderComponent::canPickParameter() const {
    const auto* owner = doc_.getTrackForLane(laneId_);
    if (owner == nullptr || host_ == nullptr)
        return false;
    return !collectAddAutomationChoices(*host_, doc_, owner->id).parameters.empty();
}

void AutomationLaneHeaderComponent::openChangeParameterPicker() { openParameterPicker(false); }

void AutomationLaneHeaderComponent::openDuplicatePicker() { openParameterPicker(true); }

// The pick arrives after the call-out closes, when this header (and the lane) may be gone, so the callback
// holds the doc, undo manager, host and lane id by value and never this. A duplicate is created only on a
// pick, in the same undo step as its curve: a dismissed picker leaves the doc exactly as it was.
void AutomationLaneHeaderComponent::openParameterPicker(bool duplicate) {
    const auto* owner = doc_.getTrackForLane(laneId_);
    if (owner == nullptr || host_ == nullptr)
        return;
    const auto choices = collectAddAutomationChoices(*host_, doc_, owner->id);
    if (choices.parameters.empty())
        return;

    auto* doc = &doc_;
    auto* undo = undo_;
    auto* host = host_;
    const auto lane = laneId_;
    auto picker = buildAddAutomationPicker(
        choices, parameterName_, [doc, undo, host, lane, duplicate](const TrackHeaderHost::AutomatableParameter& pick) {
            const auto target = host->prepareLaneTarget(pick);
            if (!target.has_value())
                return;
            if (duplicate)
                duplicateLaneUndoable(*doc, undo, lane, *target);
            else
                retargetLaneUndoable(*doc, undo, lane, *target);
        });
    const auto action = duplicate ? "Duplicate " : "Change parameter of ";
    picker->setAccessibleNames(action + parameterName_, "Search parameters for " + parameterName_);

    if (auto& hook = test_hooks::laneParameterPickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), getScreenBounds().withWidth(kIndent + kStripeWidth + 120),
                                           nullptr);
}

} // namespace synth::ui
