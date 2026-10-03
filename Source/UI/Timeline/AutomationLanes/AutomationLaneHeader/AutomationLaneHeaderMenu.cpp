// Concern: the automation lane header's edits -- the record-mode selector and the lane menu
// (Add modulator..., Change parameter..., Duplicate, Move to track, Delete lane), opened from the "..." button, a
// right-click anywhere on the header, or the keyboard. Every edit is one undo step: the lane edits go
// through AutomationLaneActions, the modulator through the host, which owns the graph.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Timeline/AutomationLanes/AddAutomation/AddAutomationPicker.h"
#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"

namespace synth::ui {

void AutomationLaneHeaderComponent::applyRecordModeChoice(int comboId) {
    setLaneRecordModeUndoable(doc_, undo_, laneId_, comboId - 1);
    applyRecordModeColour();
}

// The move targets are captured here, as the menu is built, so a click resolves against the tracks
// the user was shown even if one was added or removed while the menu was open.
juce::PopupMenu AutomationLaneHeaderComponent::buildMenu() {
    moveTargets_ = laneMoveTargets(doc_, laneId_);
    juce::PopupMenu moveMenu;
    for (int i = 0; i < (int)moveTargets_.size(); ++i)
        if (const auto* track = doc_.getTrack(moveTargets_[(size_t)i]))
            moveMenu.addItem(kMoveToTrackMenuIdBase + i, track->name);

    const bool canPick = canPickParameter();
    const juce::String noneFree = canPick ? juce::String() : juce::String(" (no free parameter)");
    juce::PopupMenu menu;
    addModulatorItem(menu);
    menu.addItem(kChangeParameterMenuId, "Change parameter..." + noneFree, canPick);
    menu.addItem(kDuplicateMenuId, "Duplicate" + noneFree, canPick);
    menu.addSeparator();
    menu.addSubMenu("Move to track", moveMenu, !moveTargets_.empty());
    menu.addSeparator();
    menu.addItem(kDeleteLaneMenuId, "Delete lane");
    return menu;
}

// A parameter with no CV jack cannot be modulated; the item stays in the menu, disabled, and says why
// in its own text (a menu item has no tooltip, and the reason must reach a screen reader too).
void AutomationLaneHeaderComponent::addModulatorItem(juce::PopupMenu& menu) const {
    const auto* lane = doc_.getLane(laneId_);
    const bool canModulate = lane != nullptr && host_ != nullptr && host_->canModulate(lane->nodeUuid, lane->paramId);
    menu.addItem(kAddModulatorMenuId,
                 canModulate ? juce::String("Add modulator...") : juce::String("Add modulator... (no CV input)"),
                 canModulate);
}

// The picker's pick comes back after the call-out closes. A new LFO and an existing one are each ONE undo step
// in the host. A stale menu cannot add a modulator to a parameter with no CV jack.
void AutomationLaneHeaderComponent::openAddModulatorPicker() {
    const auto* lane = doc_.getLane(laneId_);
    if (lane == nullptr || host_ == nullptr || !host_->canModulate(lane->nodeUuid, lane->paramId))
        return;
    const auto nodeUuid = lane->nodeUuid;
    const auto paramId = lane->paramId;
    auto* host = host_;
    const auto name = parameterName_.isNotEmpty() ? parameterName_ : paramId;
    const auto choices = collectAddModulatorChoices(host->getLfoChoices(nodeUuid, paramId), name);
    auto picker = buildAddModulatorPicker(choices, name, [host, nodeUuid, paramId](const AddModulatorPick& pick) {
        if (pick.isNew)
            host->addLfoModulator(nodeUuid, paramId);
        else
            host->connectModulator(pick.lfoUuid, nodeUuid, paramId);
    });
    if (auto& hook = test_hooks::addModulatorPickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), getScreenBounds(), nullptr);
}

// Both edits can remove this lane (and with it this component) from inside the call, so the doc,
// undo manager, lane and target are copied to locals first and the edit is the last statement.
void AutomationLaneHeaderComponent::applyMenuChoice(int menuId) {
    auto& doc = doc_;
    auto* undo = undo_;
    const auto lane = laneId_;
    if (menuId == kDeleteLaneMenuId) {
        deleteLaneUndoable(doc, undo, lane);
        return;
    }
    if (menuId == kAddModulatorMenuId) {
        openAddModulatorPicker();
        return;
    }
    if (menuId == kChangeParameterMenuId) {
        openChangeParameterPicker();
        return;
    }
    if (menuId == kDuplicateMenuId) {
        openDuplicatePicker();
        return;
    }
    const int index = menuId - kMoveToTrackMenuIdBase;
    if (index < 0 || index >= (int)moveTargets_.size())
        return;
    const auto dest = moveTargets_[(size_t)index];
    moveLaneUndoable(doc, undo, lane, dest, amountLanesTravellingWith(doc, host_, lane));
}

void AutomationLaneHeaderComponent::showMenuAt(const juce::PopupMenu::Options& options) {
    auto menu = buildMenu();
    if (auto& hook = test_hooks::laneMenuHookForTest()) {
        hook(menu, options);
        return;
    }
    juce::Component::SafePointer<AutomationLaneHeaderComponent> safeThis(this);
    menu.showMenuAsync(options, [safeThis](int result) {
        if (auto* self = safeThis.getComponent(); self != nullptr && result != 0)
            self->applyMenuChoice(result);
    });
}

// A right-click anywhere on the header opens the lane menu at the pointer; a left click on the parameter name
// opens the picker that changes what the lane controls. The record-mode combo takes its own clicks. The value readout
// forwards its events here (see the constructor).
void AutomationLaneHeaderComponent::mouseDown(const juce::MouseEvent& e) {
    const auto local = e.getEventRelativeTo(this);
    if (e.mods.isPopupMenu()) {
        showMenuAt(contextMenuOptionsAtPoint(e.getScreenPosition()));
        return;
    }
    if (!e.mods.isLeftButtonDown())
        return;
    pressed_ = true;
    pressedOnName_ = nameArea_.contains(local.getPosition());
    if (onDragPress)
        onDragPress(e.getScreenPosition().y);
}

// A press on the header is either a click (on the name it opens the parameter picker, on release) or the start of a
// drag that reorders the lane; the pool tells them apart once the pointer has travelled past the drag threshold.
void AutomationLaneHeaderComponent::mouseDrag(const juce::MouseEvent& e) {
    if (pressed_ && onDragMove)
        onDragMove(e.getScreenPosition().y);
}

void AutomationLaneHeaderComponent::mouseUp(const juce::MouseEvent& e) {
    if (!pressed_)
        return;
    pressed_ = false;
    const bool click = onDragRelease ? onDragRelease() : true;
    const bool wasName = pressedOnName_;
    pressedOnName_ = false;
    if (click && wasName && nameArea_.contains(e.getEventRelativeTo(this).getPosition()))
        openChangeParameterPicker();
}

bool AutomationLaneHeaderComponent::keyPressed(const juce::KeyPress& key, juce::Component*) {
    return onLaneKey && onLaneKey(key);
}

// Cmd+Alt+Up/Down move the lane; a bare Up/Down move focus to the row above or below; a bare Return opens the lane
// menu beside the row. Everything else bubbles on.
bool AutomationLaneHeaderComponent::keyPressed(const juce::KeyPress& key) {
    if (onLaneKey && onLaneKey(key))
        return true;
    if (key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return false;
    if (key.isKeyCode(juce::KeyPress::returnKey))
        return showContextMenuForKeyboardFocus();
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        if (onFocusMoveRequested)
            onFocusMoveRequested(key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

void AutomationLaneHeaderComponent::focusGained(juce::Component::FocusChangeType) {
    repaint();
    if (onKeyboardFocused)
        onKeyboardFocused();
}

void AutomationLaneHeaderComponent::focusLost(juce::Component::FocusChangeType) { repaint(); }

void AutomationLaneHeaderComponent::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

juce::MouseCursor AutomationLaneHeaderComponent::getMouseCursor() {
    if (host_ != nullptr && nameArea_.contains(getMouseXYRelative()))
        return juce::MouseCursor::PointingHandCursor;
    return juce::Component::getMouseCursor();
}

// Shift+F10 (or the menu key) or Return on the row, or Shift+F10 on the record-mode combo: the menu opens beside the
// focused control, the way a right-click would open it at the pointer.
bool AutomationLaneHeaderComponent::showContextMenuForKeyboardFocus() {
    if (doc_.getLane(laneId_) == nullptr)
        return false;
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const auto anchor = focused != nullptr && isParentOf(focused) ? focused->getScreenBounds() : getScreenBounds();
    showMenuAt(contextMenuOptions(anchor));
    return true;
}

} // namespace synth::ui

namespace synth::ui::test_hooks {
std::function<void(const juce::PopupMenu&, const juce::PopupMenu::Options&)>& laneMenuHookForTest() {
    static std::function<void(const juce::PopupMenu&, const juce::PopupMenu::Options&)> hook;
    return hook;
}
} // namespace synth::ui::test_hooks
