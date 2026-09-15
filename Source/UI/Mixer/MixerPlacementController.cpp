// Concern: FRO12 (P9-6) -- the Mixer placement preference (Tab/Own panel/Window) and moving
// mixerHost_ between its three homes.
#include "MixerPlacementController.h"

namespace synth::ui {

MixerPlacementController::MixerPlacementController(MixerDockComponent& mixerDock,
                                                   juce::ApplicationProperties& appProperties)
    : mixerDock_(mixerDock)
    , appProperties_(appProperties) {
    setVisible(false); // Own-panel's strip; hidden until applyPlacementPreference() says otherwise
}

MixerPlacementController::Placement MixerPlacementController::readPersistedPlacement() const {
    if (appProperties_.getUserSettings() == nullptr)
        return Placement::Tab;
    const auto s = appProperties_.getUserSettings()->getValue(kMixerPlacementKey, "tab");
    if (s == "ownPanel")
        return Placement::OwnPanel;
    if (s == "window")
        return Placement::Window;
    return Placement::Tab;
}

void MixerPlacementController::applyPlacementPreference() {
    const auto desired = readPersistedPlacement();
    if (desired == placement_)
        return; // already there -- also what stops this from doing real work on every window
                // move/resize's persist-triggered ChangeListener re-notification (see
                // DetachedPanelWindow::persistBounds)
    applyPlacement(desired);
}

void MixerPlacementController::applyPlacement(Placement placement) {
    auto& host = mixerDock_.getMixerHost();
    switch (placement) {
    case Placement::Tab:
        mixerDock_.addAndMakeVisible(host); // reclaims it (no-op if already there)
        host.setEmbeddedHeader(true);
        mixerDock_.setMixerTabEnabled(true);
        setVisible(false);
        break;
    case Placement::Window:
        // Stays parented inside mixerDock_ (harmless -- it renders nothing once detached; see
        // DetachablePanelHost::resized()), just hidden there via the disabled tab. Never
        // eagerly detached here -- "opened on first reveal", see revealOrToggle().
        mixerDock_.addAndMakeVisible(host);
        host.setEmbeddedHeader(true);
        mixerDock_.setMixerTabEnabled(false);
        setVisible(false);
        break;
    case Placement::OwnPanel:
        mixerDock_.setMixerTabEnabled(false);
        addAndMakeVisible(host); // reparents INTO this strip
        host.setEmbeddedHeader(false);
        setVisible(true);
        break;
    }
    placement_ = placement;
    resized();
}

bool MixerPlacementController::revealOrToggle() {
    switch (placement_) {
    case Placement::Tab:
        return false; // caller keeps its own open/close-the-dock behaviour
    case Placement::OwnPanel:
        setVisible(!isVisible());
        return true;
    case Placement::Window:
        mixerDock_.getMixerHost().setDetached(!mixerDock_.getMixerHost().isDetached());
        return true;
    }
    return false;
}

void MixerPlacementController::resized() {
    if (placement_ != Placement::OwnPanel)
        return;
    mixerDock_.getMixerHost().setBounds(getLocalBounds());
}

} // namespace synth::ui
