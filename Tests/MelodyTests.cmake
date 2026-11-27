# The hum-to-melody tests, in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Timeline/HumToMelodyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/MainComponentConvertToMelodyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Timeline/TimelineClipLane/ConvertToMelodyMenuTests.cpp
)
