# The More row a cable drag opened folds away again and its cable ends glide (docs/layout/module-card.md#the-more-row), kept in its own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CableFold/CableFoldTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CableFold/CableEndGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CableFold/CableDropRefusalTests.cpp
)
include(${CMAKE_CURRENT_SOURCE_DIR}/CommandPaletteTests.cmake)
include(${CMAKE_CURRENT_SOURCE_DIR}/ControllerSurfaceTests.cmake)
