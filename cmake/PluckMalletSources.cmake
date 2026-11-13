# The Pluck and Mallet instruments (a plucked string and a struck bar, each with eight voices on one MIDI
# track). Part of Core; their own list so the root file stays under the line cap. ModTargetList (the shared
# knob-cable list every instrument declares its cables with) lives here for the same reason.
target_sources(Core PRIVATE
    Source/Modules/ModTargetList.cpp
    Source/Modules/InstrumentVoiceUtil.h
    Source/Modules/Pluck/PluckModule.cpp
    Source/Modules/Pluck/PluckModule.h
    Source/Modules/Pluck/PluckVoices.h
    Source/Modules/Mallet/MalletModule.cpp
    Source/Modules/Mallet/MalletModule.h
    Source/Modules/Mallet/MalletVoices.h
)
