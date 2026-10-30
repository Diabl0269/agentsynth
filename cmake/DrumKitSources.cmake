# The Drum Kit instrument (synthesized drums on one MIDI track, or a recorded kit). Part of Core; its own list so the root file
# stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/DrumKit/DrumKitModule.cpp
    Source/Modules/DrumKit/DrumKitModuleRecorded.cpp
    Source/Modules/DrumKit/DrumKitModulePads.cpp
    Source/Modules/DrumKit/DrumKitModulePadAudio.cpp
    Source/Modules/DrumKit/DrumKitModuleInstrumentPads.cpp
    Source/Modules/DrumKit/DrumKitPads.cpp
    Source/Modules/DrumKit/DrumKitPads.h
    Source/Modules/DrumKit/DrumKitPadVoice.h
    Source/Modules/DrumKit/DrumKitModule.h
    Source/Modules/DrumKit/DrumVoices.h
)
