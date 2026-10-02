// TimelinePanelShapes.cpp
//
// The Draw tool's shapes: the shape strip beside the Draw button, the Shift+digit shape keys, Draw
// pressed again stepping through the shapes, and the panel-level keys of the lane range (Delete,
// Escape). TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp
// files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

namespace {

juce::String shapeActionIdFor(DrawShape shape) { return "timelineShape" + juce::String(drawShapeName(shape)); }

juce::KeyPress shapeFallbackKey(DrawShape shape) {
    return juce::KeyPress('0' + drawShapeKeyDigit(shape), juce::ModifierKeys::shiftModifier, 0);
}

} // namespace

void TimelinePanelComponent::initDrawShapes() {
    addChildComponent(shapeStrip_);
    shapeStrip_.onShapeClicked = [this](DrawShape shape) { pickDrawShape(shape); };
    shapeStrip_.onSlideFrame = [this] { layoutTransportRow(); };
    // One range at a time across the whole timeline: a lane range starting drops the clip range.
    automationLanes_.onLaneRangeChanged = [this] {
        if (automationLanes_.getLaneRange().isActive())
            clipLaneArea_.clearRange();
        updateShapeStripShowing();
    };
    // A press anywhere in the clip lanes is a "click elsewhere" for the lane range, a clip range included.
    clipLaneArea_.onPressed = [this] { automationLanes_.getLaneRange().clear(); };
}

// The strip slides out of the Draw button while Draw is the tool, and also while a lane range is selected
// (whatever the tool), so Range-drag then a click on a shape stamps it without visiting Draw first.
void TimelinePanelComponent::updateShapeStripShowing() {
    shapeStrip_.setShowing(activeTool_ == EditTool::Draw || automationLanes_.hasLaneRange());
}

void TimelinePanelComponent::setDrawShape(DrawShape shape) {
    drawShape_ = shape;
    automationLanes_.setDrawShape(shape);
    shapeStrip_.setActiveShape(shape);
}

// The lane range survives the switch to Draw (tools never clear it), so Range-drag, then a shape, stamps
// the shape over the span the person just chose.
void TimelinePanelComponent::pickDrawShape(DrawShape shape) {
    setDrawShape(shape);
    if (activeTool_ != EditTool::Draw)
        setActiveTool(EditTool::Draw);
    if (automationLanes_.hasLaneRange())
        automationLanes_.stampShapeOnLaneRange(shape);
}

void TimelinePanelComponent::refreshShapeTooltips() {
    for (auto shape : kAllDrawShapes)
        if (auto* button = shapeStrip_.getButton(shape))
            button->setTooltip(synth::ui::formatShortcutHint(
                juce::String(drawShapeName(shape)) + " shape",
                shortcutHintFor(shortcuts_, shapeActionIdFor(shape), shapeFallbackKey(shape))));
}

// Runs before the tool digits in keyPressed(): with no manager installed, a Shift+3 whose text character
// is missing would otherwise read as the bare 3 (Split).
bool TimelinePanelComponent::handleDrawShapeKey(const juce::KeyPress& key) {
    for (auto shape : kAllDrawShapes) {
        if (matchesAction(key, shapeActionIdFor(shape), shapeFallbackKey(shape))) {
            pickDrawShape(shape);
            return true;
        }
    }

    // The Draw key while Draw is already the tool steps to the next shape, wrapping.
    if (activeTool_ == EditTool::Draw) {
        const auto drawKey = juce::KeyPress('0' + editToolKeyDigit(EditTool::Draw), juce::ModifierKeys::noModifiers, 0);
        bool isDrawKey = false;
        if (shortcuts_ != nullptr) {
            isDrawKey = matchesAction(key, "timelineToolDraw", drawKey);
        } else if (!key.getModifiers().isCommandDown()) {
            const int typed = (int)key.getTextCharacter();
            isDrawKey =
                (typed != 0 ? typed : key.getKeyCode()) == drawKey.getKeyCode() && !key.getModifiers().isShiftDown();
        }
        if (isDrawKey) {
            setDrawShape(nextDrawShape(drawShape_));
            return true;
        }
    }

    const bool plain = key.getModifiers().withoutMouseButtons() == juce::ModifierKeys();
    if (plain && (key.getKeyCode() == juce::KeyPress::deleteKey || key.getKeyCode() == juce::KeyPress::backspaceKey) &&
        automationLanes_.hasLaneRange()) {
        automationLanes_.deleteLaneRangePoints();
        return true;
    }
    if (key == juce::KeyPress::escapeKey && automationLanes_.getLaneRange().isActive()) {
        automationLanes_.getLaneRange().clear();
        return true;
    }
    return false;
}

} // namespace synth::ui
