# Recording tests. MIDI: the recorder, its per-input filters and lanes, and the track field they read. Audio: each
# track's own input (the track field, the shared capture core, the engine's input recorder, the record flow that
# runs every armed audio track at once) and hearing the input live (Monitor). Kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/MidiRecorderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/MidiRecorderInputFilterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineDoc/TimelineDocTrackMidiInputTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineDoc/TimelineDocTrackAudioInputTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TakeCaptureWriterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/InputTakeRecorderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/MainComponentAudioTakesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineDoc/TimelineDocTrackMonitorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/TrackInputMonitoringTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/TimelineRoutingPaneAudioInputTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/AudioInputMenuTrackSourceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/TimelineTrackHeaderMonitorTests.cpp
)
