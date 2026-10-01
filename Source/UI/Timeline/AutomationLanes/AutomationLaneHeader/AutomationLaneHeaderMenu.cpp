// Concern: the automation lane header's edits -- the record-mode selector and the "..." menu
// (Move to track, Delete lane). Every edit goes through AutomationLaneActions as one undo step.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

#include "UI/Timeline/AutomationLanes/AutomationLaneActions.h"

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

    juce::PopupMenu menu;
    menu.addSubMenu("Move to track", moveMenu, !moveTargets_.empty());
    menu.addSeparator();
    menu.addItem(kDeleteLaneMenuId, "Delete lane");
    return menu;
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
    const int index = menuId - kMoveToTrackMenuIdBase;
    if (index < 0 || index >= (int)moveTargets_.size())
        return;
    const auto dest = moveTargets_[(size_t)index];
    moveLaneUndoable(doc, undo, lane, dest);
}

void AutomationLaneHeaderComponent::showMenu() {
    juce::Component::SafePointer<AutomationLaneHeaderComponent> safeThis(this);
    buildMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent(); self != nullptr && result != 0)
            self->applyMenuChoice(result);
    });
}

} // namespace synth::ui
