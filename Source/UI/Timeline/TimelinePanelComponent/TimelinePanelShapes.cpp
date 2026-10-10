// TimelinePanelShapes.cpp
//
// The Draw tool's shapes: the pen button's shape flyout, the Shift+digit shape keys, the shape-menu key,
// Draw pressed again stepping through the shapes, and the panel-level keys of the lane range (Delete,
// Escape). TimelinePanelComponent is declared in TimelinePanelComponent.h; sibling TimelinePanel*.cpp
// files in this directory hold the rest of the class.

#include "TimelinePanelComponent.h"

#include "ShortcutManager/ShortcutManager.h"
#include "UI/Layout/PopupMotion.h"

namespace synth::ui {

namespace {

juce::String shapeActionIdFor(DrawShape shape) { return "timelineShape" + juce::String(drawShapeName(shape)); }

juce::KeyPress shapeFallbackKey(DrawShape shape) {
    return juce::KeyPress('0' + drawShapeKeyDigit(shape), juce::ModifierKeys::shiftModifier, 0);
}

constexpr const char* kShapeMenuActionId = "timelineShapeMenu";
// Shift on the Draw key.
juce::KeyPress shapeMenuFallbackKey() {
    return juce::KeyPress('0' + editToolKeyDigit(EditTool::Draw), juce::ModifierKeys::shiftModifier, 0);
}

} // namespace

void TimelinePanelComponent::initDrawShapes() {
    penButton_->onOpenFlyout = [this] { openShapeFlyout(); };
    penButton_->setShape(drawShape_);
    // One range at a time across the whole timeline: a lane range starting drops the clip range.
    automationLanes_.onLaneRangeChanged = [this] {
        if (automationLanes_.getLaneRange().isActive())
            clipLaneArea_.clearRange();
    };
    // A press anywhere in the clip lanes is a "click elsewhere" for the lane range, a clip range included.
    clipLaneArea_.onPressed = [this] { automationLanes_.getLaneRange().clear(); };
}

// The flyout opens under the pen from a corner click, a held press or the shape-menu key. Opening while it is
// open closes it. A pick runs pickDrawShape after the flyout has closed; the panel may be gone by then, hence the
// safe pointer. Headless tests take the content through the hook instead of a CallOutBox.
void TimelinePanelComponent::openShapeFlyout() {
    if (shapeFlyoutBox_ != nullptr) {
        PopupMotion::dismissCallOut(*shapeFlyoutBox_);
        return;
    }
    juce::Component::SafePointer<TimelinePanelComponent> safe(this);
    auto flyout = std::make_unique<DrawShapeFlyout>(
        drawShape_,
        [this](DrawShape shape) {
            return shortcutHintFor(shortcuts_, shapeActionIdFor(shape), shapeFallbackKey(shape));
        },
        [safe](DrawShape shape) {
            if (safe != nullptr)
                safe->pickDrawShape(shape);
        });
    if (auto& hook = test_hooks::drawShapeFlyoutHookForTest()) {
        hook(std::move(flyout));
        return;
    }
    auto& box = juce::CallOutBox::launchAsynchronously(std::move(flyout), penButton_->getScreenBounds(), nullptr);
    shapeFlyoutBox_ = &box;
}

void TimelinePanelComponent::setDrawShape(DrawShape shape) {
    drawShape_ = shape;
    automationLanes_.setDrawShape(shape);
    penButton_->setShape(shape);
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

// The Draw button's tooltip names the flyout's key beside the tool's own.
void TimelinePanelComponent::refreshShapeTooltips() {
    const auto drawKey =
        shortcutHintFor(shortcuts_, "timelineToolDraw",
                        juce::KeyPress('0' + editToolKeyDigit(EditTool::Draw), juce::ModifierKeys(), 0));
    const auto menuKey = shortcutHintFor(shortcuts_, kShapeMenuActionId, shapeMenuFallbackKey());
    auto tip = synth::ui::formatShortcutHint(editToolName(EditTool::Draw), drawKey);
    if (menuKey.isNotEmpty())
        tip += juce::String::fromUTF8(" \xc2\xb7 shapes: ") + menuKey;
    penButton_->setTooltip(tip);
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
    if (matchesAction(key, kShapeMenuActionId, shapeMenuFallbackKey())) {
        openShapeFlyout();
        return true;
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
