# Recording audio from each track's own input: the track field, the shared capture core, the engine's input
# recorder, and the record flow that runs every armed audio track at once. Kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TimelineDoc/TimelineDocTrackAudioInputTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/TakeCaptureWriterTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/InputTakeRecorderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/MainComponentAudioTakesTests.cpp
)
