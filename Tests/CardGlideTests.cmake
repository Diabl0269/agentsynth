# The card make-room glide, delete gap and undo/redo motion tests (docs/layout/animation.md), plus the Edit Layout
# editor's undo, hide and follow motion tests and its group move and rename tests, kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideUndoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideDeleteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroDeleteTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroGapTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideMacroTakeOutUndoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardGlide/CardGlideFrameCostTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardEditorUndoGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupMoveTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupMoveEditorTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupRenameTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardGroupHitTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardEditorHideMotionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/CardLayoutEditor/OnCard/OnCardEditorFollowTests.cpp
)
