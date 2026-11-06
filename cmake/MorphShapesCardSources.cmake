# The morph card and the Morph Shapes card (with its curve view), the Filter cutoff's Snap to notes, and the Filter
# shape window's pitch grid and Ring helpers. Split out of AppUISources.cmake to keep that list under the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModuleComponent/ModuleComponentMorphCard.cpp
    Source/UI/Graph/ModuleComponent/CutoffNoteSnap.cpp
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.cpp
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCardCurve.cpp
    Source/UI/ModuleViews/ShapeEditor/PitchGrid.cpp
    Source/UI/ModuleViews/ShapeEditor/PitchGridRows.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeRingHelpers.cpp
    Source/UI/ModuleViews/ShapeEditor/ShapeListEditorRing.cpp
)
