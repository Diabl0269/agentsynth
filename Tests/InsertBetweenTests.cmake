# A module dragged (or added from the keyboard) between other cards pushes them aside
# (docs/layout/layout.md#making-room-for-a-module-dropped-between-others), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapPlanTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapHoverTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapDropTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapMacroTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/InsertGap/InsertGapMotionTests.cpp
)
