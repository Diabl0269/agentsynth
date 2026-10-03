// Concern: the modulator row's edits -- every control writes its parameter through the host's undoable
// parameter path -- the live-value refresh that keeps the controls in step with the canvas card, and the
// "..." menu (Show on canvas, Remove modulator).
#include "UI/Timeline/AutomationLanes/Modulators/ModulatorRow.h"

#include "UI/Timeline/AutomationLanes/LaneMenuHook.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include <cmath>

namespace synth::ui {

void ModulatorRow::edit(const juce::String& uuid, const juce::String& paramId, float value, ParameterEditPhase phase) {
    if (host_ != nullptr && uuid.isNotEmpty())
        host_->setNodeParameter(uuid, paramId, value, phase);
}

// A drag is one undo step (Begin at the press, Change per move, End at the release), exactly as a
// canvas knob's gesture is. A value change with no drag around it -- a key press, a double-click reset,
// an accessibility "set value" -- is a complete edit of its own (Once).
void ModulatorRow::wireDragEdits(juce::Slider& slider, const juce::String& uuid, const juce::String& paramId,
                                 std::function<float(double)> toParameter) {
    slider.onDragStart = [this, &slider, uuid, paramId, toParameter] {
        dragging_ = &slider;
        edit(uuid, paramId, toParameter(slider.getValue()), ParameterEditPhase::Begin);
    };
    slider.onValueChange = [this, &slider, uuid, paramId, toParameter] {
        const auto phase = dragging_ == &slider ? ParameterEditPhase::Change : ParameterEditPhase::Once;
        edit(uuid, paramId, toParameter(slider.getValue()), phase);
        repaint(slider.getBounds()); // the value text is drawn over the bar
    };
    slider.onDragEnd = [this, &slider, uuid, paramId, toParameter] {
        dragging_ = nullptr;
        edit(uuid, paramId, toParameter(slider.getValue()), ParameterEditPhase::End);
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
        syncRate_.setVisible(synced);
        rateHz_.setVisible(!synced);
        if (dragging_ != &rateHz_)
            set(rateHz_, host_->getNodeParameter(lfo, "rateHz"));
    }
}

juce::PopupMenu ModulatorRow::buildMenu() const {
    juce::PopupMenu menu;
    menu.addItem(kShowOnCanvasMenuId, "Show on canvas");
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
    else if (menuId == kRemoveMenuId)
        host->removeModulator(info);
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
    if (e.mods.isPopupMenu())
        showMenuAt(contextMenuOptionsAtPoint(e.getScreenPosition()));
}

// A bare Up/Down on the row itself moves focus to the row above or below (the track list's order), and a bare Return
// opens its menu; the row's controls keep their own keys because those never reach here.
bool ModulatorRow::keyPressed(const juce::KeyPress& key) {
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
