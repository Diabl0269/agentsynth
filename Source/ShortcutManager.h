#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AppCommands {
enum CommandIDs {
    openSettings = 0x100,
    savePreset,
    openPreset,
    newPatch,
    undo,
    redo,
    toggleModMatrix,
    toggleMinimap,
    toggleAiPanel,
    autoArrange,
    toggleLibrary,
    selectAllModules,
    saveSnippet,
    copySelection,
    pasteSelection,
    duplicateSelection,
    toggleTimelinePanel,
    // Not user-rebindable (no ShortcutManager actionId/binding) — Sparkle's own convention is a
    // plain "Check for Updates…" menu item with no keyboard shortcut. macOS only; see
    // Source/Update/UpdateManager.h.
    checkForUpdates
};

inline juce::CommandID getCommandForAction(const juce::String& actionId) {
    if (actionId == "openSettings")
        return openSettings;
    if (actionId == "savePreset")
        return savePreset;
    if (actionId == "openPreset")
        return openPreset;
    if (actionId == "newPatch")
        return newPatch;
    if (actionId == "undo")
        return undo;
    if (actionId == "redo")
        return redo;
    if (actionId == "toggleModMatrix")
        return toggleModMatrix;
    if (actionId == "toggleMinimap")
        return toggleMinimap;
    if (actionId == "toggleAiPanel")
        return toggleAiPanel;
    if (actionId == "autoArrange")
        return autoArrange;
    if (actionId == "toggleLibrary")
        return toggleLibrary;
    if (actionId == "selectAllModules")
        return selectAllModules;
    if (actionId == "saveSnippet")
        return saveSnippet;
    if (actionId == "copySelection")
        return copySelection;
    if (actionId == "pasteSelection")
        return pasteSelection;
    if (actionId == "duplicateSelection")
        return duplicateSelection;
    if (actionId == "toggleTimelinePanel")
        return toggleTimelinePanel;
    return 0;
}
} // namespace AppCommands

class ShortcutManager {
public:
    ShortcutManager() { resetToDefaults(); }

    void loadFromProperties(juce::ApplicationProperties& props) {
        appProperties = &props;
        auto* settings = props.getUserSettings();
        if (settings == nullptr)
            return;

        for (auto& actionId : actionIds) {
            auto key = "shortcut_" + actionId;
            if (settings->containsKey(key))
                bindings[actionId] = parseKeyPress(settings->getValue(key));
        }
    }

    void saveToProperties() {
        if (appProperties == nullptr)
            return;
        auto* settings = appProperties->getUserSettings();
        if (settings == nullptr)
            return;

        for (auto& actionId : actionIds) {
            settings->setValue("shortcut_" + actionId, encodeKeyPress(bindings.at(actionId)));
        }
        appProperties->saveIfNeeded();
        if (onBindingsChanged)
            onBindingsChanged();
    }

    juce::KeyPress getBinding(const juce::String& actionId) const {
        auto it = bindings.find(actionId);
        return it != bindings.end() ? it->second : juce::KeyPress();
    }

    juce::String getActionForKeyPress(const juce::KeyPress& key) const {
        for (auto& [actionId, binding] : bindings) {
            if (towlower(binding.getKeyCode()) == towlower(key.getKeyCode()) &&
                binding.getModifiers() == key.getModifiers())
                return actionId;
        }
        return {};
    }

    void setBinding(const juce::String& actionId, const juce::KeyPress& key) { bindings[actionId] = key; }

    juce::String getConflictingAction(const juce::String& actionId, const juce::KeyPress& key) const {
        for (auto& [otherId, binding] : bindings) {
            if (otherId != actionId && towlower(binding.getKeyCode()) == towlower(key.getKeyCode()) &&
                binding.getModifiers() == key.getModifiers())
                return otherId;
        }
        return {};
    }

    void resetToDefaults() {
        bindings.clear();
        bindings["openSettings"] = juce::KeyPress(',', juce::ModifierKeys::commandModifier, 0);
        bindings["savePreset"] = juce::KeyPress('s', juce::ModifierKeys::commandModifier, 0);
        bindings["openPreset"] = juce::KeyPress('o', juce::ModifierKeys::commandModifier, 0);
        bindings["newPatch"] = juce::KeyPress('n', juce::ModifierKeys::commandModifier, 0);
        bindings["undo"] = juce::KeyPress('z', juce::ModifierKeys::commandModifier, 0);
        bindings["redo"] =
            juce::KeyPress('z', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        bindings["toggleModMatrix"] = juce::KeyPress('m', juce::ModifierKeys::commandModifier, 0);
        // 'k' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M
        // / A / L / B, Shift+A, Shift+S) — safe to claim for the minimap toggle (issue #159).
        bindings["toggleMinimap"] = juce::KeyPress('k', juce::ModifierKeys::commandModifier, 0);
        bindings["toggleAiPanel"] = juce::KeyPress('a', juce::ModifierKeys::commandModifier, 0);
        bindings["autoArrange"] = juce::KeyPress('l', juce::ModifierKeys::commandModifier, 0);
        bindings["toggleLibrary"] = juce::KeyPress('b', juce::ModifierKeys::commandModifier, 0);
        // Cmd+A and Cmd+S are already taken (toggleAiPanel / savePreset), hence the Shift variants.
        bindings["selectAllModules"] =
            juce::KeyPress('a', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        bindings["saveSnippet"] =
            juce::KeyPress('s', juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier, 0);
        // The platform-standard trio. Safe to claim app-wide because JUCE's TextEditor consumes
        // Cmd+C/Cmd+V itself while it has focus, so these only reach the canvas when no text field
        // is being edited — see MainComponent::keyPressed, which is the sole dispatch point.
        bindings["copySelection"] = juce::KeyPress('c', juce::ModifierKeys::commandModifier, 0);
        bindings["pasteSelection"] = juce::KeyPress('v', juce::ModifierKeys::commandModifier, 0);
        bindings["duplicateSelection"] = juce::KeyPress('d', juce::ModifierKeys::commandModifier, 0);
        // 't' with plain Cmd is unused by any other binding (Cmd+, / S / O / N / Z / Shift+Z / M /
        // K / A / L / B, Shift+A, Shift+S, C / V / D) — safe to claim for the timeline panel
        // toggle (TL5-1).
        bindings["toggleTimelinePanel"] = juce::KeyPress('t', juce::ModifierKeys::commandModifier, 0);
    }

    static juce::String keyPressToDisplayString(const juce::KeyPress& key) {
        juce::String result;
        auto mods = key.getModifiers();

#if JUCE_MAC
        if (mods.isCommandDown())
            result += "Cmd + ";
        if (mods.isCtrlDown())
            result += "Ctrl + ";
#else
        if (mods.isCtrlDown())
            result += "Ctrl + ";
#endif
        if (mods.isAltDown())
            result += "Alt + ";
        if (mods.isShiftDown())
            result += "Shift + ";

        auto keyCode = key.getKeyCode();
        if (keyCode >= 'a' && keyCode <= 'z')
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode - 32));
        else if (keyCode >= 'A' && keyCode <= 'Z')
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode));
        else if (keyCode == ',')
            result += ",";
        else if (keyCode == '.')
            result += ".";
        else if (keyCode == '/')
            result += "/";
        else if (keyCode == ';')
            result += ";";
        else if (keyCode == '\'')
            result += "'";
        else if (keyCode == '[')
            result += "[";
        else if (keyCode == ']')
            result += "]";
        else if (keyCode == '-')
            result += "-";
        else if (keyCode == '=')
            result += "=";
        else
            result += juce::String::charToString(static_cast<juce::juce_wchar>(keyCode));

        return result;
    }

    static juce::String getActionDescription(const juce::String& actionId) {
        if (actionId == "openSettings")
            return "Open Settings";
        if (actionId == "savePreset")
            return "Save Preset";
        if (actionId == "openPreset")
            return "Open Preset";
        if (actionId == "newPatch")
            return "New Patch";
        if (actionId == "undo")
            return "Undo";
        if (actionId == "redo")
            return "Redo";
        if (actionId == "toggleModMatrix")
            return "Toggle Mod Matrix";
        if (actionId == "toggleMinimap")
            return "Toggle Minimap";
        if (actionId == "toggleAiPanel")
            return "Toggle AI Panel";
        if (actionId == "autoArrange")
            return "Auto Arrange";
        if (actionId == "toggleLibrary")
            return "Toggle Module Library";
        if (actionId == "selectAllModules")
            return "Select All Modules";
        if (actionId == "saveSnippet")
            return "Save Selection as Snippet";
        if (actionId == "copySelection")
            return "Copy Selected Modules";
        if (actionId == "pasteSelection")
            return "Paste Modules";
        if (actionId == "duplicateSelection")
            return "Duplicate Selected Modules";
        if (actionId == "toggleTimelinePanel")
            return "Toggle Timeline Panel";
        return actionId;
    }

    static juce::KeyPress parseKeyPress(const juce::String& encoded) {
        auto parts = juce::StringArray::fromTokens(encoded, ":", "");
        if (parts.size() == 2)
            return juce::KeyPress(parts[0].getIntValue(), juce::ModifierKeys(parts[1].getIntValue()), 0);
        return {};
    }

    static juce::String encodeKeyPress(const juce::KeyPress& key) {
        return juce::String(key.getKeyCode()) + ":" + juce::String(key.getModifiers().getRawFlags());
    }

    const juce::StringArray& getActionIds() const { return actionIds; }

    std::function<void()> onBindingsChanged;

private:
    std::map<juce::String, juce::KeyPress> bindings;
    juce::ApplicationProperties* appProperties = nullptr;

    juce::StringArray actionIds{"openSettings",
                                "savePreset",
                                "openPreset",
                                "newPatch",
                                "undo",
                                "redo",
                                "toggleModMatrix",
                                "toggleMinimap",
                                "toggleAiPanel",
                                "autoArrange",
                                "toggleLibrary",
                                "selectAllModules",
                                "saveSnippet",
                                "copySelection",
                                "pasteSelection",
                                "duplicateSelection",
                                "toggleTimelinePanel"};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ShortcutManager)
};
