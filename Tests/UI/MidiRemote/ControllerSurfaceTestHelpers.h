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

// A sparser layout for group-drag tests -- fourControlModel()'s dense 2x2 means nearly
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

// 8 pads in a single row, cols 0-7 -- unlike twoAdjacentPlusBystanderModel()'s
// bystander parked far away (col 5), a drag's intermediate whole-cell steps here cross cells
// OCCUPIED by unselected pads (pad5..pad8), which is exactly the path twoAdjacentPlusBystanderModel()
// can't exercise: a group drag whose live intermediate positions overlap other controls before
// landing on a free destination.
inline std::vector<synth::ui::ControllerSurfaceComponent::CellModel> eightPadsInARowModel() {
    std::vector<synth::ui::ControllerSurfaceComponent::CellModel> cells;
    for (int i = 0; i < 8; ++i)
        cells.push_back(
            makeCellModel("pad" + juce::String(i + 1), "Pad " + juce::String(i + 1), synth::ControlKind::pad, i, 0));
    return cells;
}

// Cells are grandchildren of `surface` (they live on its private content_ child, which
// carries the pan/zoom transform -- ControllerSurfaceComponent.h), not direct children, so this
// goes through the component's own test seam rather than surface.getChildren().
inline synth::ui::ControllerSurfaceCell* findCell(synth::ui::ControllerSurfaceComponent& surface,
                                                  const juce::String& controlId) {
    return surface.findCellForTest(controlId);
}

inline juce::MouseEvent surfaceMouseEvent(juce::Component& comp, juce::Point<float> pos,
                                          juce::Point<float> mouseDownPos, bool wasDragged,
                                          juce::ModifierKeys mods = juce::ModifierKeys()) {
    return juce::MouseEvent(juce::Desktop::getInstance().getMainMouseSource(), pos, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &comp, &comp, juce::Time::getCurrentTime(), mouseDownPos, juce::Time::getCurrentTime(), 1,
                            wasDragged);
}

// Drives a REAL multi-step drag on `cell` -- mouseDown, then one mouseDrag per entry of
// `parentOffsetsInCells` (each a (dCols, dRows) offset from the drag's start, in whole cells, not
// cumulative-since-last-step) -- computing each step's LOCAL mouse position the same way a real
// juce::MouseInputSource would: from a position that is fixed in the PARENT's coordinate space
// (real screen coordinates don't move just because the dragged component's bounds do) converted
// into the cell's CURRENT bounds, which the owner may have just changed in response to the
// previous step. A single-shot drag (one mouseDrag straight from start to end) can't exercise the
// mid-drag-reposition hazard; only reading the cell's position fresh before
// each step can. Leaves the cell mid-drag (call cell.mouseUp(...) with the LAST event this
// function fires, e.g. via lastStepEvent()) -- see the test suites for the pattern.
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
    // kCellPitch (kCellSize + the inter-cell margin), NOT kCellSize alone: the on-screen distance
    // between adjacent cells is the pitch, matching what ControllerSurfaceCell::mouseDrag itself
    // now divides by (see its own bugfix comment) -- stepping by kCellSize alone would feed the
    // cell exactly what its old, buggy divisor expected and could never have caught that bug.
    void stepToOffset(int dCols, int dRows) {
        const auto cellPitch = (float)synth::ui::ControllerSurfaceCell::kCellPitch;
        const auto targetParent = downParent_ + juce::Point<float>((float)dCols * cellPitch, (float)dRows * cellPitch);
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

// Like DragDriver, but originates every step's mouse position in the SURFACE's own local
// space -- this suite's stand-in for real OS screen coordinates, since the surface is the root
// component in every headless test here -- and converts it into the cell's local space via
// juce::Component::getLocalPoint(), the exact conversion JUCE performs when it delivers a real
// event through an intervening transform. DragDriver deliberately never does this (it stays
// entirely in parent-local units, by design -- see its own comment), so it cannot tell a correct
// pan/zoom coordinate conversion from a broken one; this class is what a "drag lands where dropped
// under zoom/pan" test actually needs. `dCols`/`dRows` are a CANVAS-space (content-local) offset in
// whole cells from the drag's start, scaled by the surface's own current zoom to get the
// equivalent on-screen distance -- exactly what a real mouse doing "N cells' worth of on-screen
// travel at this zoom level" would produce.
class ScreenSpaceDragDriver {
public:
    ScreenSpaceDragDriver(synth::ui::ControllerSurfaceComponent& surface, juce::Component& cell)
        : surface_(surface)
        , cell_(cell) {
        downScreen_ = surface_.getLocalPoint(&cell_, cell_.getLocalBounds().getCentre().toFloat());
        downLocal_ = cell_.getLocalPoint(&surface_, downScreen_);
        lastLocal_ = downLocal_;
        cell_.mouseDown(surfaceMouseEvent(cell_, downLocal_, downLocal_, false));
    }

    void stepToCanvasOffset(int dCols, int dRows) {
        const float zoom = surface_.getZoomLevelForTest();
        const auto cellPitch = (float)synth::ui::ControllerSurfaceCell::kCellPitch;
        const auto targetScreen =
            downScreen_ + juce::Point<float>((float)dCols * cellPitch, (float)dRows * cellPitch) * zoom;
        lastLocal_ = cell_.getLocalPoint(&surface_, targetScreen);
        cell_.mouseDrag(surfaceMouseEvent(cell_, lastLocal_, downLocal_, true));
    }

    void end() { cell_.mouseUp(surfaceMouseEvent(cell_, lastLocal_, downLocal_, true)); }

private:
    synth::ui::ControllerSurfaceComponent& surface_;
    juce::Component& cell_;
    juce::Point<float> downScreen_;
    juce::Point<float> downLocal_;
    juce::Point<float> lastLocal_;
};

} // namespace midiremote_surface_test
