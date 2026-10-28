# The shape editing tests kept out of Tests/CMakeLists.txt so it stays under the file-size cap: the curve editor's
# multi-point selection (docs/layout/visualizers.md) and the Custom filter's Pitch cable for the per-note modes (the
# upstream lookup and the shape window's By hand / By pitch choice, docs/modules/custom-filter.md).
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/CurveEditor/CurveEditorMultiSelectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mixer/ChannelFlow/UpstreamPitchSourceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/PerNotePitchCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowGridTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowSpectrumTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowThemeTests.cpp
)
