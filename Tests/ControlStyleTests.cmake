# The Depth look and its picker, and the knob and fader styles (Settings > Appearance > Controls) and a module's own choice of them, kept in their own
# list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/KnobStyleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/DepthLevelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Settings/DepthPickerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/App/MainComponent/DepthSettingIntegrationTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Theme/ControlStyleOverrideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ControlStyle/ControlStyleNodeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ControlStyle/ControlStyleCardTests.cpp
)
