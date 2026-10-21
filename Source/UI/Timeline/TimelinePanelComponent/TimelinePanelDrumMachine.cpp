// TimelinePanelComponent: the drum machine as the second clip editor. It takes the piano roll's rect and cross-fade,
// and the panel's one predicate, clipEditorOpen(), answers "is an editor covering the clip lanes" for every branch that
// used to ask the roll alone.

#include "TimelinePanelComponent.h"
#include "UI/DrumMachine/DrumMachineHost.h"

namespace synth::ui {

bool TimelinePanelComponent::clipEditorOpen() const noexcept {
    return pianoRoll_.isOpen() || (drum_ != nullptr && drum_->grid.isOpen());
}

DrumMachineComponent& TimelinePanelComponent::getDrumMachine() const noexcept { return drum_->grid; }

bool TimelinePanelComponent::isDrumKitClip(synth::ClipId id) const {
    const auto* track = doc_ != nullptr ? doc_->getTrackForClip(id) : nullptr;
    return track != nullptr && trackHeaderHost_ != nullptr && trackHeaderHost_->isDrumKitTrack(track->id);
}

void TimelinePanelComponent::wireDrumMachine() {
    drum_ = std::make_unique<DrumMachineHost>();
    auto& grid = drum_->grid;
    addChildComponent(grid);
    grid.setComponentID("timelineDrumMachine");
    grid.onAboutToClose = [this] { rollLeaving_.capture(drum_->grid); };
    grid.onCloseRequested = [this] { closeDrumMachine(); };
    grid.onHorizontalViewChanged = [this] {
        if (drum_->grid.isOpen())
            ruler_.setMappingOverride(&drum_->grid.getViewState(), drum_->grid.leftGutterWidth());
        ruler_.repaint();
    };
    grid.onPianoRollRequested = [this](synth::ClipId id) {
        closeDrumMachine();
        openPianoRoll(id);
    };
    grid.onAuditionNote = [this](int pitch, float velocity01, bool on) {
        auditionClipNote(drum_->grid.getClipId(), pitch, velocity01, on);
    };
    pianoRoll_.onDrumMachineRequested = [this](synth::ClipId id) {
        closePianoRoll();
        openDrumMachine(id);
    };
}

void TimelinePanelComponent::openClipEditor(synth::ClipId id) {
    if (isDrumKitClip(id))
        openDrumMachine(id);
    else
        openPianoRoll(id);
}

void TimelinePanelComponent::closeClipEditor() {
    if (pianoRoll_.isOpen())
        closePianoRoll();
    if (drum_->grid.isOpen())
        closeDrumMachine();
}

void TimelinePanelComponent::openDrumMachine(synth::ClipId id) {
    if (pianoRoll_.isOpen())
        closePianoRoll();
    auto& grid = drum_->grid;
    grid.openClip(id);
    if (!grid.isOpen())
        return;
    clipLaneFade_.setShown(false);
    drum_->fade.setShown(true);
    playhead_.setLocalPlayheadClient(&grid);
    grid.grabKeyboardFocus();
    resized(); // the toolbar row above the ruler appears, exactly as for the piano roll
    ruler_.setMappingOverride(&grid.getViewState(), grid.leftGutterWidth());
    playhead_.repaint();
}

// Leaving cross-fades a picture of the grid as it was over the lanes, as closePianoRoll does for the roll.
void TimelinePanelComponent::closeDrumMachine() {
    auto& grid = drum_->grid;
    if (grid.isOpen())
        rollLeaving_.capture(grid);
    grid.closeGrid();
    if (rollLeaving_.play())
        drum_->fade.snapTo(false);
    else
        drum_->fade.setShown(false);
    clipLaneFade_.setShown(true);
    clipLaneArea_.grabKeyboardFocus();
    resized();
    ruler_.setMappingOverride(nullptr, 0);
    playhead_.repaint();
}

} // namespace synth::ui
