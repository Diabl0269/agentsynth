# Appends the shortcut manager, and the small units that keep it out of widely included headers, to
# APPUI_SOURCES. Paths are relative to the repo root.
list(APPEND APPUI_SOURCES
    Source/ShortcutManager/AppCommands.cpp
    Source/ShortcutManager/AppCommands.h
    Source/ShortcutManager/ShortcutManager.h
    Source/ShortcutManager/ShortcutManager.cpp
    Source/ShortcutManager/ShortcutManagerActionNames.cpp
    Source/ShortcutManager/ShortcutManagerActionTable.cpp
    Source/ShortcutManager/ShortcutManagerDefaults.cpp
    Source/ShortcutManager/ShortcutManagerMigrations.cpp
    Source/UI/Layout/FocusRegion.cpp
    Source/UI/Layout/KeyboardContextMenu.cpp
    Source/UI/Library/ModuleLibraryHelpPopup.cpp
)
