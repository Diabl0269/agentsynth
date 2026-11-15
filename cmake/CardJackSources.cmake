# A card's jacks: where they are laid out, and the Dual I/O split and merge glide that eases them (and the cables on
# them) to a new layout. Appended to APPUI_SOURCES by AppUISources.cmake.
list(APPEND APPUI_SOURCES
    Source/UI/Graph/ModuleComponent/ModuleComponentJackLayout.cpp
    Source/UI/Graph/ModuleComponent/ModuleComponentJackMemo.h
    Source/UI/Graph/ModuleComponent/DualIOJackGlide.h
    Source/UI/Graph/ModuleComponent/DualIOJackGlide.cpp
)
