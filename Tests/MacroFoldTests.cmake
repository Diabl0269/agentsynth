# The macro fold motion tests (docs/layout/macro-fold-motion.md), kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldTimelineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldPackTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldNestedTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldDeepNestTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldCableSlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/MacroFold/MacroFoldOpenNeighbourTests.cpp
)
