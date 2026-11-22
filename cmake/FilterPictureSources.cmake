# The Filter card's response picture and its two icon toggles (docs/layout/module-card-layout.md#the-filter-picture),
# appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Graph/CardWidgets/CardIconToggle.h
    Source/UI/Graph/CardWidgets/CardIconToggle.cpp
    Source/UI/Graph/ModuleComponent/ModuleComponentFilterPicture.cpp
)
