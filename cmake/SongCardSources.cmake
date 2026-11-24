# The song cards' step blocks, thumbs and details-panel style, and every AI card's action row, appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Assistant/AIChatComponent/StepBlocks.h
    Source/UI/Assistant/AIChatComponent/StepBlocks.cpp
    Source/UI/Assistant/AIChatComponent/SongCardThumbs.h
    Source/UI/Assistant/AIChatComponent/SongCardThumbs.cpp
    Source/UI/Assistant/AIChatComponent/DetailsPanelStyle.h
    Source/UI/Assistant/AIChatComponent/DetailsPanelStyle.cpp
    Source/UI/Assistant/AIChatComponent/CardActionRow.h
    Source/UI/Assistant/AIChatComponent/CardActionRow.cpp
)
