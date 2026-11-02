// Concern: the tool cursors of a lane surface (see LaneToolCursors.h).
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"

#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/ToolCursors.h"
#include <memory>

namespace synth::ui {

namespace {
synth::theme::Icon iconFor(EditTool tool) noexcept {
    switch (tool) {
    case EditTool::Range:
        return synth::theme::Icon::ToolRange;
    case EditTool::Erase:
        return synth::theme::Icon::ToolErase;
    default:
        return synth::theme::Icon::ToolDraw;
    }
}
} // namespace

juce::MouseCursor LaneToolCursors::get(const juce::Component& owner, EditTool tool) {
    if (!hasCursor(tool))
        return juce::MouseCursor(juce::MouseCursor::NormalCursor);
    const auto index = (std::size_t)tool;
    if (!built_[index]) {
        auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&owner.getLookAndFeel());
        std::unique_ptr<juce::Drawable> icon = lf != nullptr ? lf->getIcon(iconFor(tool)) : nullptr;
        cursors_[index] = makeToolCursor(tool, icon.get()); // null-safe: a headless build has no icons
        built_[index] = true;
    }
    return cursors_[index];
}

juce::String laneCursorKind(const juce::MouseCursor& cursor) {
    if (cursor == juce::MouseCursor(juce::MouseCursor::NormalCursor))
        return "arrow";
    return cursor == dragGrabCursor() ? "grab" : "tool";
}

namespace {
class LaneStateProbe
    : public juce::Component
    , public synth::testmode::StateSource {
public:
    LaneStateProbe(const juce::String& id, std::function<void(juce::DynamicObject&)> fill)
        : fill_(std::move(fill)) {
        setComponentID(id);
        setInterceptsMouseClicks(false, false);
        setAccessible(false);
        setBounds(0, 0, 1, 1);
    }
    void addTestModeState(juce::DynamicObject& into) const override { fill_(into); }

private:
    std::function<void(juce::DynamicObject&)> fill_;
};
} // namespace

std::unique_ptr<juce::Component> makeLaneStateProbe(const juce::String& id,
                                                    std::function<void(juce::DynamicObject&)> fill) {
    return std::make_unique<LaneStateProbe>(id, std::move(fill));
}

} // namespace synth::ui
