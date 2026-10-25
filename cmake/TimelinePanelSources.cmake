# The timeline panel's units (TimelinePanelComponent and its per-concern split). Split out of AppUISources.cmake to
# keep that list under the file-size cap; appended to APPUI_SOURCES.
list(APPEND APPUI_SOURCES
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h
    Source/UI/Timeline/TimelinePanelComponent/TimelineClipboardClip.h
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelStrips.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelShapes.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelClipClipboard.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelShortcuts.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelCursorGlide.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelFollowMode.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelClipKeyboard.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelLaneKeyboard.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelTrackHeaders.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelTrackDrag.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelTrackListMotion.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelTrackHeight.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelLayout.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelSidePane.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelAutomation.cpp
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelAddAutomation.cpp
    Source/UI/Timeline/TimelinePanelComponent/AddTrackPicker.cpp
)
