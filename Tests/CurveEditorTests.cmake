# The curve editor's multi-point selection tests (docs/layout/visualizers.md), kept in their own list so
# Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/CurveEditor/CurveEditorMultiSelectTests.cpp
)
