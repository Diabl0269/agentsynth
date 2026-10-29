# The theme sweep (docs/layout/theming.md, "Theme sweep guard"): every dialog, window, alert, module card and the main
# shell rendered in all built-in themes, kept in its own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepSupport.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepSurfaces.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ThemeSweep/ThemeSweepTests.cpp
)
