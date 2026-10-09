# The card make-room glide, delete gap and undo/redo motion tests (docs/layout/animation.md), kept in their own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideUndoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideDeleteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroDeleteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroGapTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroTakeOutUndoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideFrameCostTests.cpp
)
