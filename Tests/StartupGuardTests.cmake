# Launch-time guard tests (Source/MainWindowPlacement.h, Source/UnhandledExceptionLog.h), in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainWindowPlacementTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/UnhandledExceptionLogTests.cpp
)
