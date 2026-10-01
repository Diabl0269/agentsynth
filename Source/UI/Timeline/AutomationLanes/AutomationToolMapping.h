#pragma once

#include "UI/Timeline/AutomationLaneEditor.h"
#include "UI/Timeline/EditTool.h"

namespace synth::ui {

// Which curve tool an automation lane runs for the timeline's active edit tool. The lanes have no
// tool row of their own: they follow the one the clip lanes and the piano roll share, so a tool
// picked once means the same thing on every surface under it. Draw paints freehand and becomes a
// straight line while Shift is held at mouse-down; Erase deletes points; every other tool (Select,
// Range, Split, Glue, Mute) has no curve meaning and falls back to the pointer.
constexpr AutomationLaneEditor::Tool automationToolFor(EditTool tool, bool shiftDown) noexcept {
    switch (tool) {
    case EditTool::Draw:
        return shiftDown ? AutomationLaneEditor::Tool::Line : AutomationLaneEditor::Tool::Pencil;
    case EditTool::Erase:
        return AutomationLaneEditor::Tool::Eraser;
    case EditTool::Select:
    case EditTool::Range:
    case EditTool::Split:
    case EditTool::Glue:
    case EditTool::Mute:
        return AutomationLaneEditor::Tool::Pointer;
    }
    return AutomationLaneEditor::Tool::Pointer;
}

} // namespace synth::ui
