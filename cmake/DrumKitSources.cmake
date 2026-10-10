# The Drum Kit instrument (synthesized drums on one MIDI track). Part of Core; its own list so the root file
# stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/DrumKit/DrumKitModule.cpp
    Source/Modules/DrumKit/DrumKitModule.h
    Source/Modules/DrumKit/DrumVoices.h
)
