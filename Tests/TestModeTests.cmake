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
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/UiVerifyModDotFixtureTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/UiVerifyLaneRowsFixtureTests.cpp
)
# Hidden automation windows, checked on the NSWindow itself (macOS only).
if(APPLE)
    target_sources(Tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/TestMode/AutomationHidingMacTests.mm)
endif()
