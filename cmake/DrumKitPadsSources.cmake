# The Drum Kit card's pads area: the count stepper, the pad grid and the picked pad's controls. Split out of
# AppUISources.cmake to keep that list under the file-size cap.
target_sources(AppUI PRIVATE
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.h
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumKitPadsSection.cpp
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.h
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumPadControls.cpp
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.h
    Source/UI/Graph/ModuleComponent/DrumKitPads/DrumPadGrid.cpp
)
