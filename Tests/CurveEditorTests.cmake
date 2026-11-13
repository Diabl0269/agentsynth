# The shape editing tests kept out of Tests/CMakeLists.txt so it stays under the file-size cap: the curve editor's
# multi-point selection (docs/layout/visualizers.md) and the Custom filter's Pitch cable for the per-note modes (the
# upstream lookup and the shape window's By hand / By pitch choice, docs/modules/custom-filter.md), plus the layout
# registries: every drag surface and every popup user is named with its guard (docs/layout/undo-glide.md).
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/CurveEditor/CurveEditorMultiSelectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Mixer/ChannelFlow/UpstreamPitchSourceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/PerNotePitchCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowGridTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowGridLinesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeDrawGestureTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowPitchGridTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/CutoffNoteSnapTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowSpectrumTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowResponseTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeWindowThemeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeEditorKeyboardReachTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/MorphMapNamesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ShapeEditor/ShapeColourSlotTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ModalCloseKeysTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyboardReachTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/LayoutGapSweepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/KeyStandardsSweepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ReachSweepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/TabOrderReadingSweepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/EditorHeaderTabReachTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MenuFieldSpaceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/SelectFocusRingTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/ShortcutRelayTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/DialogTooltipTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/QuitRequestTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/PopoverRelayTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/DragSurfaceRegistryTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/DragCancelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/PopupMotionRegistryTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/PopupDismissPathsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionLoopUsersTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/PopupOwnPathTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/PopupHostBehaviourTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/ColourPickerKeyboardTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/ColourPickerEscapeTests.cpp
)

# Tests/CMakeLists.txt is at the file-size cap, so the modulation curve tests are pulled in from here.
include(${CMAKE_CURRENT_SOURCE_DIR}/ModCurveTests.cmake)
