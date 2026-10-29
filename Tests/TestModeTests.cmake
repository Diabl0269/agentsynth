# The in-app test mode's tests (docs/development/test-mode.md), kept in their own list so Tests/CMakeLists.txt stays
# under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeProtocolTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeLaunchTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeComponentsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeInputTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeServerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeCommandsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/TestModeCardStateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/UiVerifyFixtureTests.cpp
)
# The committed project the UI-verification examples open (Tools/ui-verify/).
target_compile_definitions(Tests PRIVATE UI_VERIFY_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tools/ui-verify/fixtures")
