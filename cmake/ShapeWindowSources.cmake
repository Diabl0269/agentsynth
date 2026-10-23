# The shape window's grid (docs/modules/custom-filter.md#shape-editor-window): the note-length grid shared with
# the LFO card, the window's toolbar row and the setting it remembers. Appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/ModuleViews/CurveEditor/CurveGridNoteLength.h
    Source/UI/ModuleViews/CurveEditor/CurveGridNoteLength.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeListEditorToolbar.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h
)
