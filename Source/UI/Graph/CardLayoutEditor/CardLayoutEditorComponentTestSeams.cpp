// CardLayoutEditorComponentTestSeams.cpp -- headless test seams. Each drives the real control, or calls
// the commit function the real control's callback calls, rather than reaching into private state.
#include "CardLayoutEditorComponent.h"
#include "CardLayoutEditorRow.h"

namespace synth::ui {

int CardLayoutEditorComponent::getVisibleRowCountForTest() const { return rows_.size(); }

CardLayoutEditorRow* CardLayoutEditorComponent::getRowForTest(int row) const {
    return row >= 0 && row < rows_.size() ? rows_[row] : nullptr;
}

juce::String CardLayoutEditorComponent::getVisibleRowParamIdForTest(int row) const {
    auto* r = getRowForTest(row);
    return r != nullptr ? r->getKey() : juce::String();
}

bool CardLayoutEditorComponent::getVisibleRowCheckedForTest(int row) const {
    auto* r = getRowForTest(row);
    return r != nullptr && r->isChecked();
}

juce::String CardLayoutEditorComponent::getVisibleRowLabelForTest(int row) const {
    auto* r = getRowForTest(row);
    return r != nullptr ? r->getLabelOverride().value_or(juce::String()) : juce::String();
}

int CardLayoutEditorComponent::findRowForTest(const juce::String& key) const {
    for (int i = 0; i < rows_.size(); ++i)
        if (rows_[i]->getKey() == key)
            return i;
    return -1;
}

// juce::TextEditor::setText's notifying path is asynchronous, so a headless test with no message pump
// would never see onTextChange run: set the text quietly and do what onTextChange does.
void CardLayoutEditorComponent::setSearchTextForTest(const juce::String& text) {
    searchEditor_.setText(text, false);
    rebuildRows();
}

void CardLayoutEditorComponent::triggerRowToggleForTest(int row) {
    if (auto* r = getRowForTest(row))
        r->triggerToggleForTest();
}

void CardLayoutEditorComponent::setRowLabelForTest(int row, const juce::String& text) {
    if (auto* r = getRowForTest(row))
        r->setLabelTextForTest(text);
}

void CardLayoutEditorComponent::commitRowLabelForTest(int row) {
    if (auto* r = getRowForTest(row))
        r->commitLabelForTest();
}

// What a real drag's release does once it has worked out the target, without synthesizing the mouse
// sequence (MacroPortConfigDialog::dragRowToIndexInGroupForTest is the precedent).
void CardLayoutEditorComponent::dragCheckedRowToIndexForTest(const juce::String& paramId, int newIndexAmongChecked) {
    model_.moveToIndex(paramId, newIndexAmongChecked);
    commitAndRebuild();
}

juce::Component* CardLayoutEditorComponent::getRowDragHandleForTest(int row) {
    auto* r = getRowForTest(row);
    return r != nullptr ? &r->getDragHandleForTest() : nullptr;
}

juce::Rectangle<int> CardLayoutEditorComponent::getRowBoundsForTest(int row) const {
    auto* r = getRowForTest(row);
    return r != nullptr ? r->getBounds() : juce::Rectangle<int>();
}

bool CardLayoutEditorComponent::pressKeyOnRowForTest(int row, const juce::KeyPress& key) {
    auto* r = getRowForTest(row);
    return r != nullptr && r->keyPressed(key);
}

void CardLayoutEditorComponent::selectWidgetForTest(int row, CardWidget widget) {
    auto* r = getRowForTest(row);
    if (r == nullptr)
        return;
    auto& combo = r->getWidgetComboForTest();
    for (int i = 0; i < combo.getNumItems(); ++i)
        if (combo.getItemText(i) == cardLayoutWidgetName(widget))
            combo.setSelectedId(combo.getItemId(i), juce::sendNotificationSync);
}

void CardLayoutEditorComponent::triggerAddGroupForTest() {
    if (addGroupButton_.onClick)
        addGroupButton_.onClick();
}

void CardLayoutEditorComponent::setApplyToAllInstancesForTest(bool allInstances) {
    applyToCombo_.setSelectedId(allInstances ? 2 : 1, juce::sendNotificationSync);
}

juce::StringArray CardLayoutEditorComponent::getPresetNamesForTest() const {
    juce::StringArray names;
    for (int i = 0; i < presetCombo_.getNumItems(); ++i)
        names.add(presetCombo_.getItemText(i));
    return names;
}

// Matched by item text; setSelectedId fires onChange synchronously whatever the combo's text mode.
void CardLayoutEditorComponent::selectPresetForTest(const juce::String& name) {
    for (int i = 0; i < presetCombo_.getNumItems(); ++i) {
        if (presetCombo_.getItemText(i) == name) {
            presetCombo_.setSelectedId(presetCombo_.getItemId(i), juce::sendNotificationSync);
            return;
        }
    }
}

void CardLayoutEditorComponent::triggerSaveAsPresetForTest(const juce::String& name) { commitSaveAsPreset(name); }

void CardLayoutEditorComponent::triggerDeletePresetForTest(const juce::String& name) { commitDeletePreset(name); }

void CardLayoutEditorComponent::triggerResetToAutomaticForTest() { resetToDefault(); }

} // namespace synth::ui
