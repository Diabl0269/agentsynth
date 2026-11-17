# The Edit Layout reorder-drag animator, session and viewport follower tests, kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ReorderDrag/ReorderDragAnimatorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ReorderDrag/ReorderDragSessionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ReorderDrag/ViewportDragFollowerTests.cpp
)
