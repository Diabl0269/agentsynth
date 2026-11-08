# A Drum Kit pad's sound: pads that play another module, the pad's Sound menu, sounds dropped on a pad, and MIDI Learn
# on the pad knobs. Kept in its own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/DrumKit/DrumKitInstrumentPadsRenderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/DrumKit/DrumKitInstrumentPadsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/DrumKitPadSoundCardTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteDrumPadLearnTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/MidiRemote/MidiRemoteWorkflowRig.h
)
