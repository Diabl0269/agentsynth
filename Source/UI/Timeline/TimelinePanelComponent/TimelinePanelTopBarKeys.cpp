// Concern: which controls the Timeline's top-bar key ring (synth::ui::KeyRing) stops on.
#include "UI/Layout/KeyRing.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"

namespace synth::ui {

void TimelinePanelComponent::focusLost(FocusChangeType) { topBarKeys_->leave(); }

std::unique_ptr<juce::AccessibilityHandler> TimelinePanelComponent::createAccessibilityHandler() {
    return topBarKeys_->createAccessibilityHandler();
}

// Every control of the top bar. Add Track is not here: it is the first stop of the track column below, reached with
// Down.
std::vector<juce::Component*> TimelinePanelComponent::topBarStops() {
    std::vector<juce::Component*> stops{&sidePaneButton_, &clipSizingButton_,     &snapToggleButton_,
                                        &snapCombo_,      &followPlayheadButton_, dockBack_};
    for (auto tool : kAllEditTools)
        stops.push_back(getToolButton(tool));
    stops.insert(stops.end(),
                 {&transportBar_.getReturnToStartButton(), &transportBar_.getPlayStopButton(),
                  &transportBar_.getRecordButton(), &transportBar_.getLoopButton(), &transportBar_.getMetronomeButton(),
                  &transportBar_.getCountInCombo(), &transportBar_.getBpmLabel(), &transportBar_.getTimeSigLabel(),
                  &transportBar_.getSongKeyField()});
    return stops;
}

} // namespace synth::ui
