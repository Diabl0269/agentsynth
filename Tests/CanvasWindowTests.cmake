# The canvas window's tests (docs/layout/chrome.md#the-canvas-window), kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/CanvasWindow/CanvasWindowDetachTests.cpp
)
