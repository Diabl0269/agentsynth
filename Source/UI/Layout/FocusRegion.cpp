#include "UI/Layout/FocusRegion.h"

#include "ShortcutManager/ShortcutManager.h"

namespace synth::ui {

std::optional<bool> resolveFocusCycleKeyPress(const juce::KeyPress& key, const ShortcutManager& shortcutManager) {
    for (const auto& action : shortcutManager.getActionsForKeyPress(key)) {
        if (action == "focusNextRegion")
            return true;
        if (action == "focusPrevRegion")
            return false;
    }
    return std::nullopt;
}

} // namespace synth::ui
