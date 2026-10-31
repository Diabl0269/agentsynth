# The More row a cable drag opened folds away again (docs/layout/module-card.md#the-more-row), kept in its own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CableFold/CableFoldTests.cpp
)
