# The song key's tests, in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Music/SongKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/SongKeyNotesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/MainComponentSongKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyFieldTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyAuditionerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyAuditionOwnersTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyAuditionRenderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyPickerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/SongKeyFieldTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/PianoRoll/PianoRollSongKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/ClipKeyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/FitToKeyTests.cpp
)
include(${CMAKE_CURRENT_SOURCE_DIR}/SequencerKeyTests.cmake)
