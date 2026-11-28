# The Depth picker (Settings > Appearance > Controls) appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the
# file-size cap). Its model, DepthLevel, is in cmake/KnobStyleSources.cmake because Core compiles it. Also the card
# panel picture, which re-renders when the depth (or any theme) changes.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModuleComponent/CardPanelPicture.h
    Source/UI/Graph/ModuleComponent/CardPanelPicture.cpp
    Source/UI/Settings/DepthPicker.h
    Source/UI/Settings/DepthPicker.cpp
    Source/UI/Settings/AppearanceSettingsTab/AppearanceSettingsTabControls.cpp
)
