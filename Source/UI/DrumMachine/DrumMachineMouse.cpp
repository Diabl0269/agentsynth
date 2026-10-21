// DrumMachineComponent: the mouse. A press on an empty cell adds a hit at once; a press on a hit waits to see what
// it is: released in place it removes the hit (Shift: toggles its accent), dragged up or down it sets the loudness,
// previewed on screen and committed once on release.

#include "DrumMachineComponent.h"

#include <algorithm>
#include <cmath>

namespace synth::ui {

int DrumMachineComponent::getCellVelocity(int row, int step) const {
    const int pitch = getLanePitch(row);
    if (pitch < 0)
        return 0;
    if (gesture_.kind == Gesture::Kind::Velocity && gesture_.cell == Cell{row, step})
        return gesture_.previewVelocity;
    const auto it = cells_.find(drumgrid::cellKey(pitch, step));
    return it == cells_.end() ? 0 : it->second.velocity;
}

void DrumMachineComponent::mouseDown(const juce::MouseEvent& e) {
    grabKeyboardFocus();
    if (e.mods.isPopupMenu() || !clipId_.isValid())
        return;
    const auto cell = cellAt(e.getPosition());
    if (!cell)
        return;
    focus_ = *cell;
    const int pitch = getLanePitch(cell->row);
    if (pitch < 0)
        return;
    const bool occupied = cells_.count(drumgrid::cellKey(pitch, cell->step)) > 0;
    if (!occupied) {
        addHit(pitch, cell->step, e.mods.isShiftDown() ? drumgrid::kAccentVelocity : drumgrid::kNewHitVelocity);
        return;
    }
    gesture_ = {};
    gesture_.kind = Gesture::Kind::PendingHit;
    gesture_.cell = *cell;
    gesture_.pitch = pitch;
    gesture_.startVelocity = getCellVelocity(cell->row, cell->step);
    gesture_.previewVelocity = gesture_.startVelocity;
    gesture_.startY = e.getPosition().y;
    gesture_.shift = e.mods.isShiftDown();
    repaint();
}

void DrumMachineComponent::mouseDrag(const juce::MouseEvent& e) {
    if (gesture_.kind == Gesture::Kind::None)
        return;
    const int dy = gesture_.startY - e.getPosition().y; // up is louder
    if (gesture_.kind == Gesture::Kind::PendingHit) {
        if (std::abs(dy) < kVelocityDragThresholdPx)
            return;
        gesture_.kind = Gesture::Kind::Velocity;
    }
    const int velocity =
        juce::jlimit(1, 127, gesture_.startVelocity + static_cast<int>(std::lround(dy * kVelocityPerPixel)));
    if (velocity == gesture_.previewVelocity)
        return;
    gesture_.previewVelocity = velocity;
    repaint(cellBounds(gesture_.cell.row, gesture_.cell.step).expanded(2));
}

void DrumMachineComponent::mouseUp(const juce::MouseEvent&) { endGesture(true); }

// Commits (or, with `commit` false, drops) the gesture in flight; a velocity drag becomes one undo step here.
void DrumMachineComponent::endGesture(bool commit) {
    const auto gesture = gesture_;
    gesture_ = {};
    if (gesture.kind == Gesture::Kind::None)
        return;
    if (!commit) {
        repaint();
        return;
    }
    if (gesture.kind == Gesture::Kind::PendingHit) {
        if (gesture.shift)
            toggleAccent(gesture.pitch, gesture.cell.step);
        else
            removeHits(gesture.pitch, gesture.cell.step);
        return;
    }
    if (gesture.previewVelocity != gesture.startVelocity) {
        setCellVelocity(gesture.pitch, gesture.cell.step, gesture.previewVelocity);
        audition(gesture.pitch, gesture.previewVelocity);
    } else {
        repaint();
    }
}

} // namespace synth::ui
