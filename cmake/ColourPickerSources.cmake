# The colour picker popover's own units (docs/development/accessibility.md#the-colour-picker), appended to APPUI_SOURCES
# (included from cmake/ShapeWindowSources.cmake, before AppUI is created).
list(APPEND APPUI_SOURCES
    Source/UI/Chrome/ColourPickerPopupShow.cpp
    Source/UI/Chrome/ColourPickerFields.h
    Source/UI/Chrome/ColourPickerFields.cpp
)
