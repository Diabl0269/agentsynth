#include "UI/Library/ModuleLibraryHelpPopup.h"

#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

juce::StringArray ModuleLibraryHelpPopup::shortcutLines(const ShortcutManager* manager) {
    juce::StringArray lines;
    for (const auto& entry : curatedShortcuts()) {
        const auto key = shortcutHintFor(manager, entry.actionId, entry.fallback);
        const auto label = ShortcutManager::getActionDescription(entry.actionId);
        lines.add(key.isNotEmpty() ? (label + "  -  " + key) : label);
    }
    return lines;
}

} // namespace synth::ui
