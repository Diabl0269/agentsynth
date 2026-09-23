// Concern: FRO135 pick-target session -- the panel's "Pick a module control" flow
// (docs/control/midi-remote-ui.md#assign-from-the-panel-control-first-learn). The overlay component
// itself is UI (Source/UI/Graph/PickTargetOverlay/); this is the session that feeds it candidates
// from every learnable surface and turns the click it reports into an assignment.

#include "MidiRemote/MidiLearnController.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Graph/PickTargetOverlay/GraphPickCandidates.h"
#include "UI/Graph/PickTargetOverlay/PickTargetOverlay.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Timeline/TimelineTransportBar.h"

namespace synth::midi {

namespace {

std::vector<synth::ui::PickCandidate> collectAllPickCandidates(GraphEditor& graph,
                                                               synth::ui::MixerPanelComponent* mixer,
                                                               synth::ui::TimelineTransportBar* transport) {
    std::vector<synth::ui::PickCandidate> candidates;
    synth::ui::collectGraphPickCandidates(graph, candidates);
    if (mixer != nullptr)
        mixer->collectPickCandidates(candidates);
    if (transport != nullptr)
        transport->collectPickCandidates(candidates);
    return candidates;
}

} // namespace

bool MidiLearnController::isPickingTarget() const noexcept {
    return pickOverlay_ != nullptr && pickOverlay_->isActive();
}

// The dock's tab buttons pass clicks through the overlay, and a tab switch rebuilds or reveals the
// Mixer/Timeline surfaces, so the candidates collected at begin() are stale by then.
void MidiLearnController::refreshPickTarget() {
    if (isPickingTarget())
        pickOverlay_->setCandidates(collectAllPickCandidates(graphEditor_, mixerPanel_, transportBar_));
}

bool MidiLearnController::beginPickTarget(const juce::String& profileId, const juce::String& controlId) {
    if (pickOverlayHost_ == nullptr)
        return false;
    const auto* profile = findProfile(profileId);
    if (profile == nullptr)
        return false;
    const auto control = std::find_if(profile->controls.begin(), profile->controls.end(),
                                      [&](const Control& c) { return c.id == controlId; });
    if (control == profile->controls.end())
        return false;

    cancelArmed(); // only one learn-style mode at a time (docs/control/midi-remote-ui.md#the-learn-interaction)

    if (pickOverlay_ == nullptr) {
        pickOverlay_ = std::make_unique<synth::ui::PickTargetOverlay>();
        pickOverlay_->onPicked = [this](const PickTarget& target) {
            statusBar_.clearMessage();
            assignControl(pickProfileId_, pickControlId_, target);
        };
        pickOverlay_->onCancelled = [this] {
            statusBar_.clearMessage();
            statusBar_.showMessage("Assign cancelled");
        };
    }
    if (pickOverlay_->getParentComponent() != pickOverlayHost_.getComponent())
        pickOverlayHost_->addChildComponent(*pickOverlay_);

    pickProfileId_ = profileId;
    pickControlId_ = controlId;
    pickOverlay_->setPassThrough(pickPassThrough_);
    pickOverlay_->begin(collectAllPickCandidates(graphEditor_, mixerPanel_, transportBar_));
    statusBar_.showStickyMessage("Click the knob, slider or button that '" + control->name +
                                 "' should drive - Esc to cancel");
    return true;
}

void MidiLearnController::cancelPickTarget() {
    if (!isPickingTarget())
        return;
    pickOverlay_->end();
    statusBar_.clearMessage();
    statusBar_.showMessage("Assign cancelled");
}

} // namespace synth::midi
