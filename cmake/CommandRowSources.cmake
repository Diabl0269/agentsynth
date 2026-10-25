# MainComponent's command table rows kept outside MainComponentCommandTable.cpp, appended to APPUI_SOURCES
# (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/MainComponent/MainComponentFocusCommandRows.cpp
    Source/MainComponent/MainComponentTransportCommandRows.cpp
    Source/MainComponent/MainComponentViewSwapCommandRows.cpp
)
