# The Welcome screen tests and the Help build strip's, kept in their own list so Tests/CMakeLists.txt stays under the
# file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/HelpBuildStripTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/WelcomeScreenDismissTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/WelcomeScreenLogoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/WelcomeScreenTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Chrome/WelcomeScreenUsageStatsTests.cpp
)
