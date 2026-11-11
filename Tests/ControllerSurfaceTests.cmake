# The MIDI Remote controller surface pans under a held selection box or cell move (docs/layout/layout.md), kept in its
# own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/MidiRemote/ControllerSurfaceEdgeScrollTests.cpp
)
