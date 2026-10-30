#include "UI/Layout/KeyboardContextMenu.h"

#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

bool isOpenContextMenuKeyPress(const juce::KeyPress& key, const ShortcutManager& shortcutManager) {
    return shortcutManager.getActionsForKeyPress(key).contains("openContextMenu");
}

} // namespace synth::ui
