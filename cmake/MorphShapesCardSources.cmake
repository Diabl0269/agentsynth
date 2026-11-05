# The morph card and the Morph Shapes card (with its curve view). Split out of AppUISources.cmake to keep that list
# under the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModuleComponent/ModuleComponentMorphCard.cpp
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.h
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCard.cpp
    Source/UI/Graph/ModuleComponent/MorphShapesCard/MorphShapesCardCurve.cpp
)
