#pragma once

#include "Timeline/AutomationShapes.h"
#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/EditTool.h"
#include <array>

// The timeline's ONE edit tool (EditTool) plus the Draw tool's curve choice, mapped onto the tool a
// track automation lane row's AutomationLaneEditor uses (docs/timeline/track-automation.md#tools).
// Pure and header-only so the mapping is unit-testable without a panel.
namespace synth::ui {

// The toolbar curve selector's choices, shown while EditTool::Draw is active. Transient UI state,
// never saved -- no int-stability contract.
enum class LaneCurve { Freehand, Line, Sine, Triangle, Square, SawUp, SawDown, Random };

inline constexpr std::array<LaneCurve, 8> kAllLaneCurves{LaneCurve::Freehand, LaneCurve::Line,   LaneCurve::Sine,
                                                         LaneCurve::Triangle, LaneCurve::Square, LaneCurve::SawUp,
                                                         LaneCurve::SawDown,  LaneCurve::Random};

constexpr const char* laneCurveName(LaneCurve curve) noexcept {
    switch (curve) {
    case LaneCurve::Freehand:
        return "Freehand";
    case LaneCurve::Line:
        return "Line";
    case LaneCurve::Sine:
        return "Sine";
    case LaneCurve::Triangle:
        return "Triangle";
    case LaneCurve::Square:
        return "Square";
    case LaneCurve::SawUp:
        return "Saw Up";
    case LaneCurve::SawDown:
        return "Saw Down";
    case LaneCurve::Random:
        return "Random";
    }
    return "Freehand";
}

struct LaneEditorTool {
    AutomationLaneEditor::Tool tool = AutomationLaneEditor::Tool::Pointer;
    synth::ShapeKind shape = synth::ShapeKind::Sine; // meaningful only when tool == Tool::Shape
};

// Select -> Pointer, Erase -> Eraser, Draw -> the curve's tool (Freehand = Pencil, Line = Line, a
// waveform = Shape of that kind), and every clip-only tool (Split, Glue, Mute) -> Pointer, so a
// lane row stays editable whichever tool is active.
constexpr LaneEditorTool laneEditorToolFor(EditTool tool, LaneCurve curve) noexcept {
    using Tool = AutomationLaneEditor::Tool;
    if (tool == EditTool::Erase)
        return {Tool::Eraser, synth::ShapeKind::Sine};
    if (tool != EditTool::Draw)
        return {Tool::Pointer, synth::ShapeKind::Sine};
    switch (curve) {
    case LaneCurve::Freehand:
        return {Tool::Pencil, synth::ShapeKind::Sine};
    case LaneCurve::Line:
        return {Tool::Line, synth::ShapeKind::Sine};
    case LaneCurve::Sine:
        return {Tool::Shape, synth::ShapeKind::Sine};
    case LaneCurve::Triangle:
        return {Tool::Shape, synth::ShapeKind::Triangle};
    case LaneCurve::Square:
        return {Tool::Shape, synth::ShapeKind::Square};
    case LaneCurve::SawUp:
        return {Tool::Shape, synth::ShapeKind::SawUp};
    case LaneCurve::SawDown:
        return {Tool::Shape, synth::ShapeKind::SawDown};
    case LaneCurve::Random:
        return {Tool::Shape, synth::ShapeKind::Random};
    }
    return {Tool::Pencil, synth::ShapeKind::Sine};
}

} // namespace synth::ui
