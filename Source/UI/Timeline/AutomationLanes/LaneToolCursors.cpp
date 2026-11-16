// Concern: the tool cursors of a lane surface (see LaneToolCursors.h).
#include "UI/Timeline/AutomationLanes/LaneToolCursors.h"

#include "TestMode/TestModeStateSource.h"
#include "UI/Layout/DragCursor.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Timeline/AutomationLanes/LaneShapes/DrawShapeIcons.h"
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

juce::String shapeCursorName(DrawShape shape) { return "tool:" + juce::String(drawShapeName(shape)).toLowerCase(); }

// The shape's icon, tinted like the tool icons, with the hot spot on the point where its stroke starts so the first
// drawn point lands under the pointer. An empty cursor (a shape with no icon) tells the caller to use the pen.
juce::MouseCursor buildShapeCursor(const juce::Component& owner, DrawShape shape) {
    const auto path = drawShapeIconPath(shape);
    if (path.isEmpty())
        return {};
    constexpr int kSize = 24;
    juce::Image image(juce::Image::ARGB, kSize, kSize, true);
    juce::Graphics g(image);
    paintDrawShapeIcon(g, shape, juce::Rectangle<float>(0.0f, 0.0f, (float)kSize, (float)kSize),
                       synth::theme::themeOf(owner).colors.textPrimary);
    juce::Path::Iterator first(path);
    first.next(); // the stroke's first point: a path starts with its moveTo
    const juce::Point<int> hotspot(juce::roundToInt(first.x1), juce::roundToInt(first.y1));
    return registerToolCursor(shapeCursorName(shape), image, hotspot);
}
} // namespace

juce::MouseCursor LaneToolCursors::get(const juce::Component& owner, EditTool tool, std::optional<DrawShape> shape) {
    if (!hasCursor(tool))
        return juce::MouseCursor(juce::MouseCursor::NormalCursor);
    if (tool == EditTool::Draw && shape.has_value() && isPeriodicShape(*shape)) {
        const auto index = (std::size_t)*shape;
        if (!shapeBuilt_[index]) {
            shapeCursors_[index] = buildShapeCursor(owner, *shape);
            shapeBuilt_[index] = true;
        }
        if (shapeCursors_[index] != juce::MouseCursor())
            return shapeCursors_[index];
    }
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
    if (cursor == dragGrabCursor())
        return "grab";
    if (const auto* image = findToolCursorImage(cursor))
        for (const auto shape : kAllDrawShapes)
            if (isPeriodicShape(shape) && image->name == shapeCursorName(shape))
                return image->name;
    return "tool";
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
