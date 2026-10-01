// Concern: the automation lane header's edits -- the record-mode selector and the "..." menu
// (Add LFO modulator, Move to track, Delete lane). Every edit is one undo step: the lane edits go
// through AutomationLaneActions, the modulator through the host, which owns the graph.
#include "UI/Timeline/AutomationLanes/AutomationLaneHeader/AutomationLaneHeaderComponent.h"

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

    juce::PopupMenu menu;
    addModulatorItem(menu);
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
    menu.addItem(kAddLfoModulatorMenuId,
                 canModulate ? juce::String("Add LFO modulator") : juce::String("Add LFO modulator (no CV input)"),
                 canModulate);
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
    if (menuId == kAddLfoModulatorMenuId) {
        const auto* target = doc.getLane(lane);
        if (target != nullptr && host_ != nullptr)
            host_->addLfoModulator(target->nodeUuid, target->paramId);
        return;
    }
    const int index = menuId - kMoveToTrackMenuIdBase;
    if (index < 0 || index >= (int)moveTargets_.size())
        return;
    const auto dest = moveTargets_[(size_t)index];
    moveLaneUndoable(doc, undo, lane, dest, sectionsLanesTravellingWith(doc, host_, lane));
}

void AutomationLaneHeaderComponent::showMenu() {
    juce::Component::SafePointer<AutomationLaneHeaderComponent> safeThis(this);
    buildMenu().showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&menuButton_), [safeThis](int result) {
        if (auto* self = safeThis.getComponent(); self != nullptr && result != 0)
            self->applyMenuChoice(result);
    });
}

} // namespace synth::ui
