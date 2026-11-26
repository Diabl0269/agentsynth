# A module dragged (or added from the keyboard) between other cards pushes them aside
# (docs/layout/layout.md#making-room-for-a-module-dropped-between-others), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapPlanTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapHoverTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapDropTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapConnectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapGhostTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapMacroTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapMotionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CableChainMotionTests.cpp
    # Every placer keeps a new card inside the canvas origin wall (docs/layout/layout.md#canvas-frame).
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/CardPlacedOffOriginTests.cpp
    # A module placed in a macro wires only that macro's cards (docs/layout/smart-connections.md).
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/SmartConnectionEngine/SmartConnectionMacroScopeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/SmartConnectionEngine/SmartConnectionSavedProjectTests.cpp
    # Turn Poly on adds one Poly MIDI per voice set and both sides sound (docs/modules/modulation.md).
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/GraphEditor/GraphEditorPolyTurnOnTests.cpp
)
# The song key's tests ride along here for the same reason.
include(${CMAKE_CURRENT_SOURCE_DIR}/SongKeyTests.cmake)
