#pragma once

#include "UI/Timeline/EditTool.h"
#include <array>
#include <functional>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

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

    // `owner` supplies the look and feel the icon is read from.
    juce::MouseCursor get(const juce::Component& owner, EditTool tool);

    // The look and feel changed: the icons re-tint, so every cursor is rebuilt on its next use.
    void invalidate() noexcept { built_.fill(false); }

private:
    std::array<juce::MouseCursor, kAllEditTools.size()> cursors_;
    std::array<bool, kAllEditTools.size()> built_{};
};

// What a lane surface's cursor is, as test mode reports it: "arrow", "grab" (a drag in progress) or "tool" (an icon or
// tool cursor such as the pen, the range cursor or the eraser).
juce::String laneCursorKind(const juce::MouseCursor& cursor);

// A 1x1 child that takes no clicks and only answers test-mode getState with what `fill` adds, so the lane surface's own
// header stays free of TestModeStateSource.h. Add it to the surface and give it `id`.
std::unique_ptr<juce::Component> makeLaneStateProbe(const juce::String& id,
                                                    std::function<void(juce::DynamicObject&)> fill);

} // namespace synth::ui
