# The shape window's grid and spectrum (docs/modules/custom-filter.md#shape-editor-window): the note-length grid shared with
# the LFO card, the live spectrum analyser shared with the Filter's response view, the window's toolbar row and the
# settings it remembers. Appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/ModuleViews/CurveEditor/CurveGridNoteLength.h
    Source/UI/ModuleViews/CurveEditor/CurveGridNoteLength.cpp
    Source/UI/ModuleViews/FilterSpectrumAnalyser.h
    Source/UI/ModuleViews/FilterSpectrumAnalyser.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeListEditorToolbar.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeListEditorLibrary.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeSavePrompts.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeSavePrompts.h
    Source/UI/ModuleViews/ShapeEditor/ShapeWindowPrefs.h
)
