# The hosted-answer song layout tests, in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/SongLayoutTests.cpp
)
