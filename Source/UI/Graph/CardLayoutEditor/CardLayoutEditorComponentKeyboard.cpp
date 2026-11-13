// CardLayoutEditorComponentKeyboard.cpp -- the list's keys: Up/Down move between rows (fixed, list-style
// like ArrowKeyNavigation), and the rebindable Layout Editor actions: Space shows or hides the focused
// control, Cmd+Up/Down moves it, Enter renames it. docs/layout/module-card-layout.md#accessibility.
#include "CardLayoutEditorComponent.h"
#include "CardLayoutEditorRow.h"
#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

namespace {

const juce::KeyPress kToggleKey(juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers, 0);
const juce::KeyPress kMoveUpKey(juce::KeyPress::upKey, juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kMoveDownKey(juce::KeyPress::downKey, juce::ModifierKeys::commandModifier, 0);
const juce::KeyPress kRenameKey(juce::KeyPress::returnKey, juce::ModifierKeys::noModifiers, 0);

} // namespace

bool CardLayoutEditorComponent::matchesAction(const juce::KeyPress& key, const char* actionId,
                                              const juce::KeyPress& fallback) const {
    if (shortcuts_ == nullptr)
        return key == fallback;
    return ShortcutManager::keyPressMatches(shortcuts_->getBinding(actionId), key);
}

// The rebindable actions are checked before the fixed arrows, so a user who binds a bare arrow to one
// of them gets it.
bool CardLayoutEditorComponent::handleRowKey(CardLayoutEditorRow& row, const juce::KeyPress& key) {
    const auto rowKey = row.getKey();
    if (matchesAction(key, "layoutEditorToggleShown", kToggleKey))
        return row.toggleChecked();
    if (matchesAction(key, "layoutEditorRename", kRenameKey))
        return row.startRename();
    const bool up = matchesAction(key, "layoutEditorMoveUp", kMoveUpKey);
    if (up || matchesAction(key, "layoutEditorMoveDown", kMoveDownKey)) {
        if (row.isHeader() || !row.isDraggable() || !model_.moveBy(rowKey, up ? -1 : 1))
            return row.isHeader() || row.isDraggable(); // consumed, so the list does not scroll instead
        commitAndRebuild(rowKey);
        return true;
    }
    if (key.getModifiers() != juce::ModifierKeys() ||
        (!key.isKeyCode(juce::KeyPress::upKey) && !key.isKeyCode(juce::KeyPress::downKey)))
        return false;
    const int index = rows_.indexOf(&row);
    const int next = juce::jlimit(0, rows_.size() - 1, index + (key.isKeyCode(juce::KeyPress::upKey) ? -1 : 1));
    if (next != index)
        focusRow(rows_[next]->getKey());
    return true;
}

// Scrolls the row into view and gives it keyboard focus (a row is the list's focus stop).
void CardLayoutEditorComponent::focusRow(const juce::String& key) {
    for (auto* row : rows_) {
        if (row->getKey() != key)
            continue;
        focusedKey_ = key;
        const auto view = rowsViewport_.getViewArea();
        if (row->getY() < view.getY())
            rowsViewport_.setViewPosition(0, row->getY());
        else if (row->getBottom() > view.getBottom())
            rowsViewport_.setViewPosition(0, row->getBottom() - view.getHeight());
        if (row->isShowing())
            row->grabKeyboardFocus();
        return;
    }
}

// Each row's tooltip names the keys as they are bound now, so a rebind never leaves a stale hint.
void CardLayoutEditorComponent::refreshShortcutTooltips() {
    const auto hint = [this](const char* id, const juce::KeyPress& fallback) {
        return shortcutHintFor(shortcuts_, id, fallback);
    };
    const auto toggle = hint("layoutEditorToggleShown", kToggleKey);
    const auto rename = hint("layoutEditorRename", kRenameKey);
    const auto moveUp = hint("layoutEditorMoveUp", kMoveUpKey);
    const auto moveDown = hint("layoutEditorMoveDown", kMoveDownKey);
    for (auto* row : rows_) {
        if (row->isHeader()) {
            row->setTooltip("A group of controls on the card. Rename it: " + rename);
            continue;
        }
        juce::StringArray parts;
        parts.add("Show or hide: " + toggle);
        if (row->isDraggable())
            parts.add("move: " + moveUp + " / " + moveDown);
        parts.add("rename: " + rename);
        row->setTooltip(row->getTitle() + ". " + parts.joinIntoString(", "));
    }
}

} // namespace synth::ui
