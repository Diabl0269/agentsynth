# Edit Layout: moving a whole group by its title and renaming it (docs/layout/module-card-layout.md), appended to
# APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditorGroupMove.cpp
    Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutOnCardEditorGroupRename.cpp
    Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutGroupHandle.cpp
    Source/UI/Graph/CardLayoutEditor/OnCard/CardLayoutGroupHandle.h
    Source/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupMove.cpp
    Source/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupMove.h
)
