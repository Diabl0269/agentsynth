#pragma once

// ControllerSurfaceTestHelpers.h -- shared builders for ControllerSurfaceComponent's headless
// tests (ControllerSurfaceTests.cpp, ControllerSurfaceSelectionTests.cpp,
// ControllerSurfaceGroupDragTests.cpp): a couple of synthetic controls at known grid positions, and
// a synthesized juce::MouseEvent through the real mouseDown/mouseDrag/mouseUp path (Source/UI/CLAUDE.md's
// "test the real mouse path" convention, Tests/Macros/MacroPortRealMouseDragTests.cpp's template).

#include "UI/MidiRemote/ControllerSurface/ControllerSurfaceComponent.h"

#include <vector>

namespace midiremote_surface_test {

inline synth::Control makeControl(const juce::String& id, const juce::String& name, synth::ControlKind kind, int col,
                                  int row) {
    synth::Control control;
    control.id = id;
    control.name = name;
    control.kind = kind;
    control.layout.col = col;
    control.layout.row = row;
    return control;
}

inline synth::ui::ControllerSurfaceComponent::CellModel makeCellModel(const juce::String& id, const juce::String& name,
                                                                      synth::ControlKind kind, int col, int row,
                                                                      const juce::String& assignmentLabel = "-",
                                                                      bool isWarning = false, bool isMapped = false,
                                                                      float initialValue = 0.0f) {
    synth::ui::ControllerSurfaceComponent::CellModel model;
    model.control = makeControl(id, name, kind, col, row);
    model.assignmentLabel = assignmentLabel;
    model.isWarning = isWarning;
    model.isMapped = isMapped;
    model.initialValue = initialValue;
    return model;
}

// Four synthetic controls covering knob/fader/pad/button at distinct grid positions, used by most
// tests. Kept small (4 cells) rather than the full 8-knob template -- nothing here exercises a real
// ControllerProfile, just the surface's own layout/selection/drag/activity code.
inline std::vector<synth::ui::ControllerSurfaceComponent::CellModel> fourControlModel() {
    return {
        makeCellModel("knob1", "Cutoff", synth::ControlKind::knob, 0, 0, "Filter - Cutoff"),
        makeCellModel("fader1", "Volume", synth::ControlKind::fader, 1, 0, "-"),
        makeCellModel("pad1", "Play", synth::ControlKind::pad, 0, 1, "Play"),
        makeCellModel("button1", "Mute", synth::ControlKind::button, 1, 1, "(missing module)", true),
    };
}

// FRO270: a sparser layout for group-drag tests -- fourControlModel()'s dense 2x2 means nearly
// every block move lands on another control, which is exactly what the overlap-refusal tests want,
// but a clamp test needs an EMPTY destination to prove it's the clamp (not the refusal) doing the
// work. Two adjacent controls at (0,0)/(1,0) plus a lone bystander far away at (5,5).
inline std::vector<synth::ui::ControllerSurfaceComponent::CellModel> twoAdjacentPlusBystanderModel() {
    return {
        makeCellModel("a", "A", synth::ControlKind::knob, 0, 0),
        makeCellModel("b", "B", synth::ControlKind::knob, 1, 0),
        makeCellModel("bystander", "Bystander", synth::ControlKind::knob, 5, 5),
    };
}

inline synth::ui::ControllerSurfaceCell* findCell(synth::ui::ControllerSurfaceComponent& surface,
                                                  const juce::String& controlId) {
    for (auto* child : surface.getChildren())
        if (auto* cell = dynamic_cast<synth::ui::ControllerSurfaceCell*>(child);
            cell != nullptr && cell->getControlId() == controlId)
            return cell;
    return nullptr;
}

inline juce::MouseEvent surfaceMouseEvent(juce::Component& comp, juce::Point<float> pos,
                                          juce::Point<float> mouseDownPos, bool wasDragged,
                                          juce::ModifierKeys mods = juce::ModifierKeys()) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

// FRO331: drives a REAL multi-step drag on `cell` -- mouseDown, then one mouseDrag per entry of
// `parentOffsetsInCells` (each a (dCols, dRows) offset from the drag's start, in whole cells, not
// cumulative-since-last-step) -- computing each step's LOCAL mouse position the same way a real
// juce::MouseInputSource would: from a position that is fixed in the PARENT's coordinate space
// (real screen coordinates don't move just because the dragged component's bounds do) converted
// into the cell's CURRENT bounds, which the owner may have just changed in response to the
// previous step. A single-shot drag (one mouseDrag straight from start to end) can't exercise the
// mid-drag-reposition hazard this ticket is about; only reading the cell's position fresh before
// each step can. Leaves the cell mid-drag (call cell.mouseUp(...) with the LAST event this
// function fires, e.g. via lastStepEvent()) -- see FRO331's test suites for the pattern.
class DragDriver {
public:
    explicit DragDriver(juce::Component& cell)
        : cell_(cell) {
        downLocal_ = cell_.getLocalBounds().getCentre().toFloat();
        downParent_ = cell_.getPosition().toFloat() + downLocal_;
        cell_.mouseDown(surfaceMouseEvent(cell_, downLocal_, downLocal_, false));
    }

    // Fires one mouseDrag for a cumulative offset of `dCols`/`dRows` WHOLE CELLS from the drag's
    // start (not from the previous step) -- mirrors what a real mouse position `parentOffsetInCells`
    // cells away from the mouseDown point produces, however many steps it takes to get there.
    void stepToOffset(int dCols, int dRows) {
        const auto cellSize = (float)synth::ui::ControllerSurfaceCell::kCellSize;
        const auto targetParent = downParent_ + juce::Point<float>((float)dCols * cellSize, (float)dRows * cellSize);
        lastLocal_ = targetParent - cell_.getPosition().toFloat(); // cell's CURRENT (possibly moved) bounds
        cell_.mouseDrag(surfaceMouseEvent(cell_, lastLocal_, downLocal_, true));
    }

    void end() { cell_.mouseUp(surfaceMouseEvent(cell_, lastLocal_, downLocal_, true)); }

private:
    juce::Component& cell_;
    juce::Point<float> downLocal_;
    juce::Point<float> downParent_;
    juce::Point<float> lastLocal_;
};

} // namespace midiremote_surface_test
