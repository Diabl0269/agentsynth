# The Mod Matrix's row grow-in and shrink-away motion (docs/layout/animation.md), appended to APPUI_SOURCES because
# cmake/AppUISources.cmake is at the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModMatrixRowMotion.h
    Source/UI/Graph/ModMatrixRowMotion.cpp
)
