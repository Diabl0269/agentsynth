// PluginKnobPickerComponentTestSeams.cpp -- headless test seams. Every one drives the real control or
// calls the same private commit function the real control's callback calls (the MacroPortConfigDialog
// idiom throughout this codebase) rather than reaching into private state directly.
#include "PluginKnobPickerComponent.h"
#include "PluginKnobPickerRow.h"
#include "PluginKnobPickerTouchCapture.h"

namespace synth::ui {

juce::String PluginKnobPickerComponent::getVisibleRowParamIdForTest(int row) const {
    return row >= 0 && row < rows_.size() ? rows_[row]->getParamId() : juce::String();
}

bool PluginKnobPickerComponent::getVisibleRowCheckedForTest(int row) const {
    return row >= 0 && row < rows_.size() && rows_[row]->isChecked();
}

juce::String PluginKnobPickerComponent::getVisibleRowLabelForTest(int row) const {
    return row >= 0 && row < rows_.size() ? rows_[row]->getLabelText() : juce::String();
}

// juce::TextEditor::setText's own "send a change" path (textChanged() -> postCommandMessage) is
// ASYNC even when asked to notify, so a headless test with no message pump would never see
// onTextChange run: set the text quietly and drive rebuildRows() directly, exactly what onTextChange
// would have done.
void PluginKnobPickerComponent::setSearchTextForTest(const juce::String& text) {
    searchEditor_.setText(text, false);
    rebuildRows();
}

void PluginKnobPickerComponent::triggerRowToggleForTest(int row) {
    if (row >= 0 && row < rows_.size())
        rows_[row]->triggerToggleForTest();
}

void PluginKnobPickerComponent::setRowLabelForTest(int row, const juce::String& text) {
    if (row >= 0 && row < rows_.size())
        rows_[row]->setLabelTextForTest(text);
}

void PluginKnobPickerComponent::commitRowLabelForTest(int row) {
    if (row >= 0 && row < rows_.size())
        rows_[row]->commitLabelForTest();
}

// Calls commitReorder directly with the given target index -- what a real drag's onDragUpdated does
// once it has computed a target row, without needing to synthesize a mouseDown/mouseDrag/mouseUp
// sequence on a row with no real on-screen peer (MacroPortConfigDialog::dragRowToIndexInGroupForTest
// is the precedent this mirrors exactly).
void PluginKnobPickerComponent::dragCheckedRowToIndexForTest(const juce::String& paramId, int newIndexAmongChecked) {
    commitReorder(paramId, newIndexAmongChecked);
}

void PluginKnobPickerComponent::setApplyToAllInstancesForTest(bool allInstances) {
    applyToCombo_.setSelectedId(allInstances ? 2 : 1, juce::sendNotificationSync);
}

juce::StringArray PluginKnobPickerComponent::getPresetNamesForTest() const {
    juce::StringArray names;
    for (int i = 0; i < presetCombo_.getNumItems(); ++i)
        names.add(presetCombo_.getItemText(i));
    return names;
}

// setSelectedId (matched by ITEM TEXT here, not a guessed id) rather than setText -- reliably fires
// onChange synchronously regardless of the combo's editable-text semantics.
void PluginKnobPickerComponent::selectPresetForTest(const juce::String& name) {
    for (int i = 0; i < presetCombo_.getNumItems(); ++i) {
        if (presetCombo_.getItemText(i) == name) {
            presetCombo_.setSelectedId(presetCombo_.getItemId(i), juce::sendNotificationSync);
            return;
        }
    }
}

void PluginKnobPickerComponent::triggerSaveAsPresetForTest(const juce::String& name) { commitSaveAsPreset(name); }

void PluginKnobPickerComponent::triggerDeletePresetForTest(const juce::String& name) { commitDeletePreset(name); }

void PluginKnobPickerComponent::triggerResetToAutomaticForTest() { resetToAutomatic(); }

// Sets the toggle's visible state and calls the same setArmed() its onClick would -- deliberately
// not relying on juce::Button::setToggleState's own click-notification semantics, so this test seam
// stays correct regardless of exactly when/whether that fires onClick.
void PluginKnobPickerComponent::setTouchToAddArmedForTest(bool armed) {
    touchToAddToggle_.setToggleState(armed, juce::dontSendNotification);
    touchCapture_->setArmed(armed);
}

bool PluginKnobPickerComponent::isTouchToAddArmedForTest() const { return touchCapture_->isArmed(); }

void PluginKnobPickerComponent::simulateTouchGestureForTest(int parameterIndex) {
    touchCapture_->simulateGestureStartForTest(parameterIndex);
}

} // namespace synth::ui
