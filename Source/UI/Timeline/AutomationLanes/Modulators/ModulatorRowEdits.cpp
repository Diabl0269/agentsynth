// Concern: the modulator row's edits -- every control writes its parameter through the host's undoable
// parameter path -- the live-value refresh that keeps the controls in step with the canvas card, and the
// "..." menu (Show on canvas, Change source..., Remove modulator).
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

#include "UI/Layout/DragCursor.h"

#include "UI/Timeline/AutomationLanes/AddModulator/AddModulatorPicker.h"
#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include "UI/Timeline/TimelineTrackHeaderComponent/TimelineTrackHeaderComponent.h"
#include <algorithm>
#include <cmath>

namespace synth::ui {

void ModulatorRow::edit(const juce::String& uuid, const juce::String& paramId, float value, ParameterEditPhase phase) {
    if (host_ != nullptr && uuid.isNotEmpty())
        host_->setNodeParameter(uuid, paramId, value, phase);
}

// A drag is one undo step (Begin at the press, Change per move, End at the release), exactly as a
// canvas knob's gesture is. A value change with no drag around it -- a key press, a double-click reset,
// an accessibility "set value" -- is a complete edit of its own (Once).
void ModulatorRow::wireDragEdits(juce::Slider& slider, const juce::String& paramId,
                                 std::function<float(double)> toParameter) {
    slider.onDragStart = [this, &slider, paramId, toParameter] {
        dragging_ = &slider;
        edit(info_.sourceUuid, paramId, toParameter(slider.getValue()), ParameterEditPhase::Begin);
    };
    slider.onValueChange = [this, &slider, paramId, toParameter] {
        const auto phase = dragging_ == &slider ? ParameterEditPhase::Change : ParameterEditPhase::Once;
        edit(info_.sourceUuid, paramId, toParameter(slider.getValue()), phase);
        repaint(slider.getBounds()); // the value text is drawn over the bar
    };
    slider.onDragEnd = [this, &slider, paramId, toParameter] {
        dragging_ = nullptr;
        edit(info_.sourceUuid, paramId, toParameter(slider.getValue()), ParameterEditPhase::End);
    };
}

// Pulled by the panel's existing transport poll (and after every graph change), so an edit on the
// canvas card shows here without a listener holding a pointer into a node an undo can free. Writes
// only what changed, without notifications, so the refresh can never itself become an edit; a changed
// control repaints itself, and with it the value text this row draws over it.
void ModulatorRow::refreshValues() {
    if (host_ == nullptr)
        return;
    const auto set = [](juce::Slider& slider, double value) {
        if (std::abs(slider.getValue() - value) > 1.0e-6)
            slider.setValue(value, juce::dontSendNotification);
    };
    if (info_.isLfo) {
        const auto& lfo = info_.sourceUuid;
        const int shape = juce::roundToInt(host_->getNodeParameter(lfo, "shape"));
        shape_.setSelectedId(shape + 1, juce::dontSendNotification);
        shapeIcon_.setShape(shape);
        syncRate_.setSelectedId(juce::roundToInt(host_->getNodeParameter(lfo, "rateSync")) + 1,
                                juce::dontSendNotification);
        const bool synced = host_->getNodeParameter(lfo, "mode") >= 0.5f;
        sync_.setToggleState(synced, juce::dontSendNotification);
        setPartVisible(syncRate_, synced && !compact_);
        setPartVisible(rateHz_, !synced && !compact_);
        if (dragging_ != &rateHz_)
            set(rateHz_, host_->getNodeParameter(lfo, "rateHz"));
    }
}

// The chips are the row menu's own items.
LaneControlStrip::Actions ModulatorRow::controlActions() {
    LaneControlStrip::Actions actions;
    actions.perform = [this](LaneControl control, juce::Component&) {
        switch (control) {
        case LaneControl::ChangeSource:
            applyMenuChoice(kChangeSourceMenuId);
            break;
        case LaneControl::ShowInCanvas:
            applyMenuChoice(kShowOnCanvasMenuId);
            break;
        case LaneControl::Remove:
            applyMenuChoice(kRemoveMenuId);
            break;
        default:
            break; // automation lane controls
        }
    };
    actions.isEnabled = [this](LaneControl control) { return controlEnabled(control); };
    actions.detail = [this](LaneControl control) {
        return control == LaneControl::ChangeSource && !controlEnabled(control)
                   ? juce::String(" (this source cannot be swapped here)")
                   : juce::String();
    };
    return actions;
}

bool ModulatorRow::controlEnabled(LaneControl control) const {
    if (control == LaneControl::ChangeSource)
        return host_ != nullptr && host_->canChangeModulatorSource(info_);
    return host_ != nullptr;
}

juce::PopupMenu ModulatorRow::buildMenu() const {
    juce::PopupMenu menu;
    menu.addItem(kShowOnCanvasMenuId, "Show on canvas");
    if (host_ != nullptr && host_->canChangeModulatorSource(info_))
        menu.addItem(kChangeSourceMenuId, "Change source...");
    menu.addSeparator();
    menu.addItem(kRemoveMenuId, "Remove modulator");
    return menu;
}

// Remove rebuilds the graph, and the refresh that follows destroys this row before the host call
// returns: the host and the routing are copied out first and the call is the last statement.
void ModulatorRow::applyMenuChoice(int menuId) {
    auto* host = host_;
    const auto info = info_;
    if (host == nullptr)
        return;
    if (menuId == kShowOnCanvasMenuId)
        host->showNodeOnCanvas(info.sourceUuid);
    else if (menuId == kChangeSourceMenuId)
        openChangeSourcePicker();
    else if (menuId == kRemoveMenuId)
        host->removeModulator(info);
}

// The pick arrives after the call-out closes, when this row may be gone (the graph refresh prunes it), so the callback
// holds the host and the routing by value and never this. The host re-points it as ONE undo step.
bool ModulatorRow::canOpenChangeSourcePicker() const {
    return host_ != nullptr && host_->canChangeModulatorSource(info_);
}

void ModulatorRow::openChangeSourcePicker() {
    if (host_ == nullptr || !host_->canChangeModulatorSource(info_))
        return;
    auto* host = host_;
    const auto info = info_;
    const auto name = parameterName_.isNotEmpty() ? parameterName_ : info.paramId;
    const auto choices = collectChangeSourceChoices(host->getChangeSourceChoices(info.targetUuid, info.paramId),
                                                    info.sourceUuid, info.sourceChannel, name);
    auto picker = buildAddModulatorPicker(choices, name, [host, info](const AddModulatorPick& pick) {
        if (pick.isNew)
            host->changeModulatorSourceToNew(info, pick.newTypeName, pick.sourceChannel);
        else
            host->changeModulatorSource(info, pick.lfoUuid, pick.sourceChannel);
    });
    picker->setAccessibleNames("Change source of " + info.sourceTitle + " on " + name, "Search modulators for " + name);
    picker->setSearchPlaceholder("Search or create a source");
    if (auto& hook = test_hooks::changeSourcePickerHookForTest()) {
        hook(std::move(picker));
        return;
    }
    juce::CallOutBox::launchAsynchronously(std::move(picker), getScreenBounds(), nullptr);
}

void ModulatorRow::showMenuAt(const juce::PopupMenu::Options& options) {
    auto menu = buildMenu();
    if (auto& hook = test_hooks::laneMenuHookForTest()) {
        hook(menu, options);
        return;
    }
    juce::Component::SafePointer<ModulatorRow> safeThis(this);
    menu.showMenuAsync(options, [safeThis](int result) {
        if (auto* self = safeThis.getComponent(); self != nullptr && result != 0)
            self->applyMenuChoice(result);
    });
}

// A right-click anywhere on the row that is not one of its controls (the shape picture counts as the row) opens the row
// menu at the pointer.
void ModulatorRow::mouseDown(const juce::MouseEvent& e) {
    if (e.mods.isPopupMenu()) {
        showMenuAt(contextMenuOptionsAtPoint(e.getScreenPosition()));
        return;
    }
    if (!e.mods.isLeftButtonDown())
        return;
    pressed_ = true;
    dragged_ = false;
    pressedOnName_ = nameButton_.getBounds().contains(e.getEventRelativeTo(this).getPosition());
    if (onDragPress)
        onDragPress(e.getScreenPosition().y);
}

// A press on the row itself (its title, its empty parts, the shape picture; never a control, which takes its own press)
// is the start of a drag that reorders the row or moves it to another lane; the owner tells a drag from a click once
// the pointer has travelled past the drag threshold.
void ModulatorRow::mouseDrag(const juce::MouseEvent& e) {
    if (!pressed_ || !onDragMove)
        return;
    const bool dragging = onDragMove(e.getScreenPosition().y);
    dragged_ = dragged_ || dragging;
    followDragCursor(*this, dragging);
}

// A press that never became a drag is a click: on the name button it opens the Change source picker, anywhere else on
// the row it gives the row keyboard focus.
void ModulatorRow::mouseUp(const juce::MouseEvent& e) {
    if (!pressed_)
        return;
    pressed_ = false;
    endDragCursor(*this);
    const bool click = !dragged_;
    const bool onName = pressedOnName_ && nameButton_.getBounds().contains(e.getEventRelativeTo(this).getPosition());
    const bool wasName = pressedOnName_;
    pressedOnName_ = false;
    dragged_ = false;
    if (click && !wasName) {
        ++clickFocusRequests_;
        grabKeyboardFocus();
    }
    if (onDragRelease)
        onDragRelease(); // a move to another lane can retire this row: nothing here touches it afterwards
    if (click && onName && nameButton_.isEnabled() && nameButton_.onClick)
        nameButton_.onClick(); // a click is no drop, so the row is still here
}

// The name button takes no mouse events, so the row tells it when the pointer is over it.
void ModulatorRow::mouseMove(const juce::MouseEvent& e) {
    nameButton_.setHovered(nameButton_.isEnabled() &&
                           nameButton_.getBounds().contains(e.getEventRelativeTo(this).getPosition()));
}

void ModulatorRow::mouseExit(const juce::MouseEvent&) { nameButton_.setHovered(false); }

juce::String ModulatorRow::getTooltip() {
    if (nameButton_.getBounds().contains(getMouseXYRelative()))
        return nameButton_.getTooltip();
    return juce::SettableTooltipClient::getTooltip();
}

void ModulatorRow::setLift(float lift) {
    if (lift == lift_)
        return;
    lift_ = lift;
    repaint();
}

// Left folds the row and Right opens it (only with the row itself focused, never from a control). A bare Up/Down on the
// row itself moves focus to the row above or below (the track list's order), and a bare Return opens its menu; the
// row's controls keep their own keys because those never reach here.
bool ModulatorRow::keyPressed(const juce::KeyPress& key) {
    if (onMoveKey && onMoveKey(key))
        return true;
    if (key.isKeyCode(juce::KeyPress::escapeKey) && (strip_.isEditing() || strip_.isRowOpen())) {
        if (strip_.isEditing())
            strip_.exitEdit();
        else
            strip_.closeRow();
        return true;
    }
    if (key.getModifiers().testFlags(juce::ModifierKeys::allKeyboardModifiers))
        return false;
    if (key.isKeyCode(juce::KeyPress::leftKey) || key.isKeyCode(juce::KeyPress::rightKey)) {
        // Only the row itself folds: a key that bubbled up from one of its controls (the arrow, the Sync toggle...) is
        // the control's own. Nothing has focus in a headless test, so that case is the only one refused here.
        auto* focused = juce::Component::getCurrentlyFocusedComponent();
        if (focused != nullptr && focused != this)
            return false;
        const bool collapse = key.isKeyCode(juce::KeyPress::leftKey);
        if (collapse == collapsed_)
            return false; // already there: Left on a folded row and Right on an open one do nothing
        if (onFoldRequested)
            onFoldRequested(collapse);
        return true;
    }
    if (key.isKeyCode(juce::KeyPress::returnKey))
        return showContextMenuForKeyboardFocus();
    if (key.isKeyCode(juce::KeyPress::upKey) || key.isKeyCode(juce::KeyPress::downKey)) {
        if (onFocusMoveRequested)
            onFocusMoveRequested(key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1);
        return true;
    }
    return false;
}

// A control of the row (a combo or bar would take the arrow key itself) offers the owner the move keys first.
bool ModulatorRow::keyPressed(const juce::KeyPress& key, juce::Component*) { return onMoveKey && onMoveKey(key); }

void ModulatorRow::focusGained(juce::Component::FocusChangeType) {
    repaint();
    if (onKeyboardFocused)
        onKeyboardFocused();
}

void ModulatorRow::focusLost(juce::Component::FocusChangeType) { repaint(); }

// Shift+F10 (or the menu key) or Return on the row, or Shift+F10 on one of its controls: the menu opens beside the
// focused control, or the row.
bool ModulatorRow::showContextMenuForKeyboardFocus() {
    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    const auto anchor = focused != nullptr && isParentOf(focused) ? focused->getScreenBounds() : getScreenBounds();
    showMenuAt(contextMenuOptions(anchor));
    return true;
}

} // namespace synth::ui
