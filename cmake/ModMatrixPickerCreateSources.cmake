# The Mod Matrix source picker's create rows (docs/layout/chrome.md), appended to APPUI_SOURCES because
# cmake/AppUISources.cmake is at the file-size cap.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModMatrixPickerCreate.h
    Source/UI/Graph/ModMatrixPickerCreate.cpp
)
