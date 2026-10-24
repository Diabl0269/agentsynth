# The Track In loop-wrap tests (docs/modules/modules.md, Track In), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineMidiSource/TimelineMidiSourceLoopTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineMidiSource/TimelineMidiSourceLoopBoundaryTests.cpp
)
