# Modulation cables on the shared output stage's Level, Pan and Mix (docs/modules/fx-modules.md) and on the
# instruments' Level (docs/modules/pluck-mallet.md), kept in their own list so Tests/CMakeLists.txt stays under the
# file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/OutputLevelCvTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/SavedCablesKeepChannelsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModDot/LevelRingMovesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/InstrumentLevelModCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/InstrumentNoteTimeCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/MacroKnobCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mixer/ChannelStripCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/KnobCableRoundTripTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/GraphEditor/GraphEditorInstrumentLevelDropTests.cpp
)
