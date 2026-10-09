# The tests of the last things that used to appear or vanish without motion (docs/layout/animation.md, "Motion rules"),
# kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionPolish/MotionPrimitivesTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionPolish/PickerFilterFadeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionPolish/ShortcutsChevronAndHintFadeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionPolish/HelpPopoverAndDetachedWindowMotionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Layout/MotionPolish/CanvasHintAndPickOutlineFadeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabChromeFadeTests.cpp
)
