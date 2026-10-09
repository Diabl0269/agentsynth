# The Keyboard Shortcuts tab's fold and search-filter motion tests (docs/layout/animation.md#fading-things-in-and-out),
# kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/ShortcutsSettingsTabFoldMotionTests.cpp
)
