# The theme sweep (docs/layout/theming.md, "Theme sweep guard"): every dialog, window, alert, module card and the main
# shell rendered in all built-in themes, kept in its own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepSurfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepGlyphTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepPopoverTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepConfirmBoxTests.cpp
    # Where the confirm dialog puts its tick box for each caller's spec.
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/ConfirmDontAskDialogLayoutTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/FocusRingConsistencyTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/DrawableButtonFocusRingTests.cpp
    # The Depth levels (Flat, Soft, Rich) on cards: what a card paints at each level and the Rich hover lift.
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/CardDepthTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/CardHoverLiftTests.cpp
    # Icon tiles (top bar chips, framed icon buttons, the toast's disc) at each Depth level.
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/IconTileDepthTests.cpp
    # The alert box's icon at each Depth level (Soft pinned against the old paintSeverityDisc call).
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/AlertIconDepthTests.cpp
    # Knob and fader caps at each Depth level (Soft pinned against the pre-change painters in ControlDepthReference.h).
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ControlDepthTests.cpp
)
