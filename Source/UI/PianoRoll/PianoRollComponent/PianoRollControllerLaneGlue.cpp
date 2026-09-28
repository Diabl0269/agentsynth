// PianoRollComponent — the roll's side of the velocity / CC lane strip: the carve-up that docks the
// strip under the note canvas, the "Lanes" chip's toggle, and the few read-only views the strip needs
// from the roll. The strip itself (painting, gestures, lane selection) is its own collaborator class,
// PianoRollControllerLanes (UI/PianoRoll/PianoRollControllerLanes/); see
// docs/timeline/piano-roll-lanes.md.

#include "PianoRollComponent.h"

#include "UI/PianoRoll/PianoRollControllerLanes/PianoRollControllerLanes.h"

namespace synth::ui {

// The strip is a child spanning the roll's FULL width, so strip-local x == roll x and it can map
// beats through the roll's own beatToX/xToBeat with no offset; its left gutter is simply
// [0, leftGutterWidth()), which follows the scale panel's slide for free. Carved from the bottom
// BEFORE the left gutter so the scale panel and the keys column end at the canvas bottom. Hidden ->
// nothing is carved, which is what keeps every pre-existing layout test (and the user's roll) exactly
// as it was without the strip.
void PianoRollComponent::layoutControllerLanes(juce::Rectangle<int>& bounds) {
    if (!controllerLanes_->isVisible()) {
        controllerLanes_->setBounds({});
        return;
    }
    const int height = std::min(PianoRollControllerLanes::kStripHeight, std::max(0, bounds.getHeight()));
    auto strip = bounds.removeFromBottom(height);
    controllerLanes_->setBounds(0, strip.getY(), getWidth(), strip.getHeight());
}

int PianoRollComponent::canvasBottom() const noexcept {
    if (controllerLanes_ != nullptr && controllerLanes_->isVisible())
        return std::max(canvasTop(), controllerLanes_->getY());
    return getHeight();
}

// Remembered for the session simply because the roll (and the strip it owns) lives as long as the
// timeline panel does; it is not persisted. Toggling re-runs the one carve-up and re-clamps the
// vertical scroll, whose lower bound depends on how tall the canvas now is.
void PianoRollComponent::setControllerLanesVisible(bool visible) {
    if (controllerLanes_->isVisible() == visible)
        return;
    if (!visible)
        controllerLanes_->cancelGesture();
    controllerLanes_->setVisible(visible);
    resized();
    setTopRowPosition(topRowPosition_);
    repaint();
}

void PianoRollComponent::toggleControllerLanes() { setControllerLanesVisible(!areControllerLanesVisible()); }

bool PianoRollComponent::areControllerLanesVisible() const noexcept { return controllerLanes_->isVisible(); }

PianoRollControllerLanes& PianoRollComponent::getControllerLanes() noexcept { return *controllerLanes_; }

juce::Rectangle<int> PianoRollComponent::getLanesButtonBounds() const noexcept { return lanesButtonBounds_; }

const NoteSelectionModel& PianoRollComponent::getSelection() const noexcept { return selection_; }

// A lane velocity drag recolours notes live (effectiveGeometryFor reads the lane's preview), so
// the notes whose preview changed are repainted — their rects only, never the whole canvas.
void PianoRollComponent::repaintNotesForLanes(const std::vector<synth::NoteId>& ids) {
    juce::Rectangle<int> dirty;
    for (const auto& id : ids) {
        const auto rect = getNoteRect(id);
        if (!rect.isEmpty())
            dirty = dirty.isEmpty() ? rect : dirty.getUnion(rect);
    }
    if (!dirty.isEmpty())
        repaint(dirty.expanded(2).getIntersection(gridRegion()));
}

double PianoRollComponent::snapBeatForLanes(double absBeat) const { return snappedBeatAt(absBeat); }

} // namespace synth::ui
