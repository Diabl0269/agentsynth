# The shared button words and tooltips (ActionTexts.h) and the tooltip rule helper (setButtonTooltip),
# appended to APPUI_SOURCES (cmake/AppUISources.cmake is at the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Layout/ActionTexts.h
    Source/UI/Layout/ButtonTooltip.h
    Source/UI/Layout/ButtonTooltip.cpp
)
