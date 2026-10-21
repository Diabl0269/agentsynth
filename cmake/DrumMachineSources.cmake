# The drum machine step-grid editor and its panel hosting, appended to AppUI's source list (kept out of
# AppUISources.cmake to stay under the file-size cap).
list(APPEND APPUI_SOURCES
    Source/UI/Timeline/TimelinePanelComponent/TimelinePanelDrumMachine.cpp
    Source/UI/DrumMachine/DrumGrid.h
    Source/UI/DrumMachine/DrumLanes.h
    Source/UI/DrumMachine/DrumLanes.cpp
    Source/UI/DrumMachine/DrumRowLabels.h
    Source/UI/DrumMachine/DrumRowLabels.cpp
    Source/UI/DrumMachine/DrumMachineComponent.h
    Source/UI/DrumMachine/DrumMachineHost.h
    Source/UI/DrumMachine/DrumMachineComponent.cpp
    Source/UI/DrumMachine/DrumMachineGeometry.cpp
    Source/UI/DrumMachine/DrumMachineEdit.cpp
    Source/UI/DrumMachine/DrumMachineMotion.cpp
    Source/UI/DrumMachine/DrumMachinePainting.cpp
    Source/UI/DrumMachine/DrumMachineMouse.cpp
    Source/UI/DrumMachine/DrumMachineKeys.cpp
    Source/UI/DrumMachine/DrumMachineAccessibility.cpp
)
