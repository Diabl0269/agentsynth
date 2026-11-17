#pragma once

#include "UI/Layout/LaneStateProbe.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShape.h"
#include "UI/Timeline/EditTool.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>

namespace synth::ui {

// The tool cursors of one lane surface (a lane's curve editor, a modulator's amount band): the same icon cursor the
// clip lanes and the piano roll show for the tool, built lazily from the themed icon so a theme change re-tints it.
// Only the tools that act on a lane get one: Range (selects a stretch), Draw (the pen) and Erase (deletes points).
// Select keeps the arrow, and Split, Glue and Mute do nothing on a lane, so they keep it too instead of promising
// an action.
class LaneToolCursors {
public:
    static constexpr bool hasCursor(EditTool tool) noexcept {
        return tool == EditTool::Range || tool == EditTool::Draw || tool == EditTool::Erase;
    }

    // `owner` supplies the look and feel the icon is read from. With the Draw tool and a periodic `shape` (Sine,
    // Triangle, Saw, Square) the cursor is that shape's icon, its hot spot on the point where the stroke starts; Free
    // and Line keep the pen.
    juce::MouseCursor get(const juce::Component& owner, EditTool tool, std::optional<DrawShape> shape = std::nullopt);

    // The look and feel changed: the icons re-tint, so every cursor is rebuilt on its next use.
    void invalidate() noexcept {
        built_.fill(false);
        shapeBuilt_.fill(false);
    }

private:
    std::array<juce::MouseCursor, kAllEditTools.size()> cursors_;
    std::array<bool, kAllEditTools.size()> built_{};
    std::array<juce::MouseCursor, kAllDrawShapes.size()> shapeCursors_;
    std::array<bool, kAllDrawShapes.size()> shapeBuilt_{};
};

// What a lane surface's cursor is, as test mode reports it: "arrow", "grab" (a drag in progress), "tool:<shape>" (a
// draw-shape pen: "tool:sine", "tool:triangle", "tool:saw", "tool:square") or "tool" (any other icon or tool cursor
// such as the pen, the range cursor or the eraser).
juce::String laneCursorKind(const juce::MouseCursor& cursor);

} // namespace synth::ui
