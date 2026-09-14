#include "MacroPortConfigDialog.h"
#include "MacroPortConfigDialogInternal.h"

namespace synth::ui {

// Concern: headless test seams that drive the real controls and read back real state.

namespace {
// Sets a combo box's selection and calls its REAL onChange handler directly, rather than via
// juce::ComboBox's own sendNotification path — that posts through AsyncUpdater, which a headless
// test's message-less run loop never pumps, so the change would silently never fire. Same idiom
// triggerRowDeleteForTest's comment already documents for juce::Button::triggerClick(). Every
// *ForTest seam that flips a combo box goes through this so "drive the real control" also means
// "and see its real, synchronous side effects" regardless of whether a message loop is running.
void setComboSelectionForTest(juce::ComboBox& box, int itemId) {
    box.setSelectedId(itemId, juce::dontSendNotification);
    if (box.onChange)
        box.onChange();
}
} // namespace

// ---- Test seams -------------------------------------------------------------------------------

juce::String MacroPortConfigDialog::getRowNodeUuidForTest(int row) const {
    return (row >= 0 && row < (int)rows_.size()) ? rows_[(size_t)row].nodeUuid : juce::String();
}

juce::String MacroPortConfigDialog::getRowNameForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) ? rowControls_[row]->nameEditor.getText() : juce::String();
}

bool MacroPortConfigDialog::getRowIsInputForTest(int row) const {
    return (row >= 0 && row < (int)rows_.size()) && rows_[(size_t)row].isInput;
}

void MacroPortConfigDialog::setNewPortNameForTest(const juce::String& name) {
    newNameEditor_.setText(name, juce::dontSendNotification);
}

void MacroPortConfigDialog::setNewPortDirectionForTest(bool isInput) {
    setComboSelectionForTest(newDirectionBox_, isInput ? kDirectionInputId : kDirectionOutputId);
}

void MacroPortConfigDialog::setNewPortKindForTest(synth::MacroPortKind kind) {
    setComboSelectionForTest(newKindBox_, kind == synth::MacroPortKind::Midi ? kKindMidiId : kKindAudioCVId);
}

void MacroPortConfigDialog::setNewPortShapeForTest(MacroPortShape shape) {
    setComboSelectionForTest(newShapeBox_, comboIndexFromShape(shape));
}

void MacroPortConfigDialog::setNewPortVoiceCountForTest(int voices) {
    newVoicesEditor_.setText(juce::String(voices), juce::dontSendNotification);
}

void MacroPortConfigDialog::triggerAddPortForTest() {
    if (!onAddPort)
        return;
    const bool isInput = newDirectionBox_.getSelectedId() == kDirectionInputId;
    const bool isMidi = newKindBox_.getSelectedId() == kKindMidiId;
    const auto kind = isMidi ? synth::MacroPortKind::Midi : synth::MacroPortKind::AudioCV;
    const auto shape = shapeFromComboIndex(newShapeBox_.getSelectedId());
    const int voices = juce::jmax(1, newVoicesEditor_.getText().getIntValue());
    onAddPort(isInput, kind, shape, voices, newNameEditor_.getText());
}

void MacroPortConfigDialog::setRowNameForTest(int row, const juce::String& name) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->nameEditor.setText(name, juce::dontSendNotification);
}

void MacroPortConfigDialog::commitRowNameForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->nameEditor.onFocusLost)
        rowControls_[row]->nameEditor.onFocusLost();
}

void MacroPortConfigDialog::triggerRowDeleteForTest(int row) {
    // Calls the REAL onClick handler directly rather than juce::Button::triggerClick(), which
    // posts an async command message (Button::handleCommandMessage) - a headless test with no
    // running message loop would never see it fire.
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->deleteButton.onClick)
        rowControls_[row]->deleteButton.onClick();
}

void MacroPortConfigDialog::setRowShapeForTest(int row, MacroPortShape shape) {
    // setComboSelectionForTest calls shapeBox.onChange directly, which now IS the commit gesture
    // (see the class comment), so this alone reproduces the real "pick a new shape" click.
    if (row >= 0 && row < (int)rowControls_.size())
        setComboSelectionForTest(rowControls_[row]->shapeBox, comboIndexFromShape(shape));
}

void MacroPortConfigDialog::setRowVoiceCountForTest(int row, int voices) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->voicesEditor.setText(juce::String(voices), juce::dontSendNotification);
}

void MacroPortConfigDialog::commitRowVoiceCountForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->voicesEditor.onFocusLost)
        rowControls_[row]->voicesEditor.onFocusLost();
}

void MacroPortConfigDialog::triggerCloseForTest() {
    if (closeButton_.onClick)
        closeButton_.onClick();
}

// ---- T152 test seams: drag-to-reorder + per-port colour -----------------------------------

void MacroPortConfigDialog::dragRowToIndexInGroupForTest(int row, int newIndexInGroup) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitDragTo(newIndexInGroup);
}

juce::Colour MacroPortConfigDialog::getRowDisplayColourForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) ? rowControls_[row]->colourSwatch.colour
                                                        : juce::Colours::transparentBlack;
}

bool MacroPortConfigDialog::getRowHasCustomColourForTest(int row) const {
    return (row >= 0 && row < (int)rowControls_.size()) && rowControls_[row]->hasCustomColourForTest();
}

void MacroPortConfigDialog::setRowColourForTest(int row, juce::Colour colour) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitColour(colour);
}

void MacroPortConfigDialog::resetRowColourForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size())
        rowControls_[row]->commitColour(std::nullopt);
}

std::unique_ptr<synth::ui::ColourPickerPopup> MacroPortConfigDialog::createRowColourPickerForTest(int row) {
    if (row < 0 || row >= (int)rowControls_.size())
        return nullptr;
    return rowControls_[row]->buildColourPicker();
}

// ---- T153 test seams: keyboard accessibility -----------------------------------------------

void MacroPortConfigDialog::simulateRowNameEscapeForTest(int row) {
    if (row >= 0 && row < (int)rowControls_.size() && rowControls_[row]->nameEditor.onEscapeKey)
        rowControls_[row]->nameEditor.onEscapeKey();
}

void MacroPortConfigDialog::simulateNewPortNameEscapeForTest() {
    if (newNameEditor_.onEscapeKey)
        newNameEditor_.onEscapeKey();
}

void MacroPortConfigDialog::simulateNewPortNameReturnForTest() {
    if (newNameEditor_.onReturnKey)
        newNameEditor_.onReturnKey();
}

int MacroPortConfigDialog::computeArrowNavigationTargetRowForTest(int fromRow, bool moveDown) const {
    return arrowNavigationTargetRow(fromRow, moveDown);
}

bool MacroPortConfigDialog::simulateRowControlArrowKeyForTest(int row, RowControl control, bool moveDown,
                                                              bool withCommandModifier) {
    if (row < 0 || row >= (int)rowControls_.size())
        return false;
    const auto mods =
        withCommandModifier ? juce::ModifierKeys(juce::ModifierKeys::commandModifier) : juce::ModifierKeys();
    const auto key = juce::KeyPress(moveDown ? juce::KeyPress::downKey : juce::KeyPress::upKey, mods, 0);
    auto* rc = rowControls_[row];
    switch (control) {
    case RowControl::Colour:
        return rc->colourSwatch.keyPressed(key);
    case RowControl::Delete:
        return rc->deleteButton.keyPressed(key);
    }
    return false;
}

// ---- Founder review round 4 test seams: focus-visibility regression coverage ------------------

void MacroPortConfigDialog::setRowFocusRingForcedForTest(int row, bool forced) {
    if (row < 0 || row >= (int)rowControls_.size())
        return;
    rowControls_[row]->deleteButton.forceFocusRingForTest = forced;
    rowControls_[row]->deleteButton.repaint();
}

juce::Image MacroPortConfigDialog::renderRowDeleteButtonForTest(int row) const {
    if (row < 0 || row >= (int)rowControls_.size())
        return {};
    auto& btn = rowControls_[row]->deleteButton;
    return btn.createComponentSnapshot(btn.getLocalBounds());
}

juce::Rectangle<int> MacroPortConfigDialog::getAddButtonBoundsForTest() const { return addButton_.getBounds(); }

} // namespace synth::ui
