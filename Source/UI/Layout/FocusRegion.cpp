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

std::optional<bool> focusCycleDirection(const juce::KeyPress& key, const ShortcutManager* shortcutManager) {
    if (shortcutManager != nullptr)
        return resolveFocusCycleKeyPress(key, *shortcutManager);
    if (key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::noModifiers, 0))
        return true;
    if (key == juce::KeyPress(juce::KeyPress::tabKey, juce::ModifierKeys::shiftModifier, 0))
        return false;
    return std::nullopt;
}

} // namespace synth::ui
