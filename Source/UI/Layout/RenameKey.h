#pragma once

#include "ShortcutManager/ShortcutManager.h"
#include <juce_gui_basics/juce_gui_basics.h>

// RenameKey.h (docs/control/shortcuts.md): the Rename key (F2, or Option+R) as every surface that owns a name reads it,
// so a track header, a mixer column and a module card answer the same chord.
namespace synth::ui {

/** True when `key` is either default Rename chord: the "renameSelectedModule" action (F2) or its second key
 *  "renameSelectedModuleAlt" (Option+R, Alt+R off the Mac). With no manager the shipped defaults are used. */
inline bool isRenameKeyPress(ShortcutManager* shortcuts, const juce::KeyPress& key) {
    if (shortcuts == nullptr)
        return key == juce::KeyPress(juce::KeyPress::F2Key) ||
               ShortcutManager::keyPressMatches(juce::KeyPress('r', juce::ModifierKeys::altModifier, 0), key);
    return ShortcutManager::keyPressMatches(shortcuts->getBinding("renameSelectedModule"), key) ||
           ShortcutManager::keyPressMatches(shortcuts->getBinding("renameSelectedModuleAlt"), key);
}

/** True while a text editor holds keyboard focus: on macOS Option+R types a symbol there, so Rename never fires. */
inline bool isTextEditorFocused() {
    return dynamic_cast<juce::TextEditor*>(juce::Component::getCurrentlyFocusedComponent()) != nullptr;
}

} // namespace synth::ui
