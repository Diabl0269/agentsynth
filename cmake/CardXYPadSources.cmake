# The Macros card's XY pad and the plot it shares with the morph map (docs/layout/module-card-layout.md#the-xy-pad),
# appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/ModuleViews/MorphMap/MorphPlot.h
    Source/UI/ModuleViews/MorphMap/MorphPlot.cpp
    Source/UI/Graph/CardWidgets/CardXYPad.h
    Source/UI/Graph/CardWidgets/CardXYPad.cpp
)
