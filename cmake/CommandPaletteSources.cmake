# The command palette (Cmd+K): its pure row ranking, its floating bar and its MainComponent wiring, appended to
# APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/CommandPalette/CommandPaletteRows.h
    Source/UI/CommandPalette/CommandPaletteRows.cpp
    Source/UI/CommandPalette/CommandPaletteComponent.h
    Source/UI/CommandPalette/CommandPaletteComponent.cpp
    Source/MainComponent/MainComponentCommandPalette.h
    Source/MainComponent/MainComponentCommandPalette.cpp
)
