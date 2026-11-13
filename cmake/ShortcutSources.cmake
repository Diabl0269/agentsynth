# Appends the shortcut manager, and the small units that keep it out of widely included headers, to
# APPUI_SOURCES. Paths are relative to the repo root.
list(APPEND APPUI_SOURCES
    Source/ShortcutManager/AppCommands.cpp
    Source/ShortcutManager/AppCommands.h
    Source/ShortcutManager/MacPeerDroppedKey.cpp
    Source/ShortcutManager/MacPeerDroppedKey.h
    Source/ShortcutManager/MacPeerDroppedKeyRelay.h
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

# The Cocoa half of the macOS keys JUCE's peer drops (Ctrl+Shift+2 is the NUL code, so no key press is ever built):
# Source/ShortcutManager/MacPeerDroppedKey{,Relay}.*. Objective-C++ with ARC, like the hosted-window key monitor.
if(APPLE)
    list(APPEND APPUI_SOURCES Source/ShortcutManager/MacPeerDroppedKeyRelay.mm)
    set_source_files_properties(Source/ShortcutManager/MacPeerDroppedKeyRelay.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
endif()
