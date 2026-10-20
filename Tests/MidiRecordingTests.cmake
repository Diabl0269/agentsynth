# MIDI recording and each track's MIDI input: the recorder, its per-input filters and lanes, and the track field
# they read. Kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/MidiRecorderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/MidiRecorderInputFilterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineDoc/TimelineDocTrackMidiInputTests.cpp
)
