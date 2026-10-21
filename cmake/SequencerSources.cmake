# The Sequencer and Poly Sequencer engine: the pattern data, its JSON and legacy-parameter adoption, the step clock,
# the player and the scale snap table. Part of Core; its own list so the root file stays under the line cap.
target_sources(Core PRIVATE
    Source/Modules/Sequencer/SequencerPattern.h
    Source/Modules/Sequencer/SequencerLanes.h
    Source/Modules/Sequencer/SequencerPattern.cpp
    Source/Modules/Sequencer/SequencerPatternParse.h
    Source/Modules/Sequencer/SequencerPatternLegacy.h
    Source/Modules/Sequencer/SequencerPatternLegacy.cpp
    Source/Modules/Sequencer/SequencerScaleSnap.h
    Source/Modules/Sequencer/SequencerScaleSnap.cpp
    Source/Modules/Sequencer/SequencerPatternOps.h
    Source/Modules/Sequencer/SequencerPatternOps.cpp
    Source/Modules/Sequencer/SequencerChords.h
    Source/Modules/Sequencer/SequencerChords.cpp
    Source/Modules/Sequencer/SequencerClock.h
    Source/Modules/Sequencer/SequencerClock.cpp
    Source/Modules/Sequencer/SequencerPlayer.h
    Source/Modules/Sequencer/SequencerPlayer.cpp
    Source/Modules/Sequencer/SequencerModuleBase.h
    Source/Modules/Sequencer/SequencerModuleBase.cpp
)
