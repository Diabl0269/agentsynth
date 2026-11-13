// AppShortcutRelay.cpp -- which keys a tool window passes on to the app, and how.
#include "AppShortcutRelay.h"
#include "ShortcutManager/AppCommands.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Graph/CanvasCardKeyboard/CanvasCardKeyboard.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"

namespace synth::ui {

namespace {
/** Commands that act on the canvas selection or the main window's own regions: with a tool window in front they would
    change something the user cannot see, or steal Tab from the window. */
bool actsOnTheMainWindowOnly(juce::CommandID command) {
    using namespace AppCommands;
    switch (command) {
    case copySelection:
    case pasteSelection:
    case cutSelection:
    case duplicateSelection:
    case selectAllModules:
    case repeatSelection:
    case groupSelection:
    case ungroupSelection:
    case collapseMacro:
    case foldAndPackMacros:
    case autoArrange:
    case saveSnippet:
    case renameSelectedModule:
    case replaceSelectedModule:
    case selectNextModule:
    case selectPreviousModule:
    case selectNextTrack:
    case selectPreviousTrack:
    case zoomInHorizontal:
    case zoomOutHorizontal:
    case zoomInVertical:
    case zoomOutVertical:
    case focusNextRegion:
    case focusPrevRegion:
    case focusTimeline:
    case focusLibrary:
    case focusLibrarySearch:
    case openContextMenu:
    case recolourFocusedItem:
    case locateMaster:
        return true;
    default:
        return false;
    }
}
} // namespace

GraphEditor* findCanvasBelow(juce::Component& root) {
    for (auto* child : root.getChildren()) {
        if (auto* canvas = dynamic_cast<GraphEditor*>(child))
            return canvas;
        if (auto* deeper = findCanvasBelow(*child))
            return deeper;
    }
    return nullptr;
}

/** True when the command `key` resolves to is undo or redo (the same resolution isWindowForwardableShortcut uses). */
bool isUndoRedoShortcut(const ShortcutManager& shortcuts, const juce::KeyPress& key) {
    for (const auto& action : shortcuts.getActionsForKeyPress(key)) {
        const auto command = AppCommands::getCommandForAction(action);
        if (command == AppCommands::kNoCommand)
            continue;
        return command == AppCommands::undo || command == AppCommands::redo;
    }
    return false;
}

/** The same resolution MainComponent::keyPressed uses: the first action bound to the key that has a command. */
bool isWindowForwardableShortcut(const ShortcutManager& shortcuts, const juce::KeyPress& key) {
    for (const auto& action : shortcuts.getActionsForKeyPress(key)) {
        const auto command = AppCommands::getCommandForAction(action);
        if (command == AppCommands::kNoCommand)
            continue;
        return !actsOnTheMainWindowOnly(command);
    }
    return false;
}

/** The target is the main component, found from the anchor when the key arrives (the card may have moved windows or
    been deleted since). A card's anchor reads the shortcut table from its canvas; a main-window panel's from the
    main window's canvas. The key is delivered asynchronously: an undo can rebuild the
    cards, which deletes the window whose key handler we are still inside. */
std::function<bool(const juce::KeyPress&)> makeAppShortcutRelay(juce::Component* anchor,
                                                                std::function<void(bool afterwards)> onUndoRedo) {
    return [safeAnchor = juce::Component::SafePointer<juce::Component>(anchor),
            onUndoRedo = std::move(onUndoRedo)](const juce::KeyPress& key) {
        if (safeAnchor == nullptr)
            return false;
        auto* canvas = dynamic_cast<GraphEditor*>(safeAnchor.getComponent());
        if (canvas == nullptr)
            canvas = safeAnchor->findParentComponentOfClass<GraphEditor>();
        // A main-window panel is not on the canvas: its canvas is the main window's, found below the command target.
        auto* target =
            dynamic_cast<juce::Component*>(safeAnchor->findParentComponentOfClass<juce::ApplicationCommandTarget>());
        if (canvas == nullptr && target != nullptr)
            canvas = findCanvasBelow(*target);
        const ShortcutManager* shortcuts = canvas != nullptr ? canvas->getCardKeyboard().getShortcutManager() : nullptr;
        if (shortcuts == nullptr || !isWindowForwardableShortcut(*shortcuts, key))
            return false;
        if (target == nullptr)
            return false;
        const bool undoRedo = onUndoRedo && isUndoRedoShortcut(*shortcuts, key);
        if (undoRedo)
            onUndoRedo(false);
        juce::MessageManager::callAsync(
            [safeTarget = juce::Component::SafePointer<juce::Component>(target), key, undoRedo, onUndoRedo] {
                if (safeTarget == nullptr)
                    return;
                safeTarget->keyPressed(key);
                if (undoRedo)
                    onUndoRedo(true);
            });
        return true;
    };
}

} // namespace synth::ui
