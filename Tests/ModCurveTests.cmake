# The modulation response curve: the type, the Attenuverter's audio path and its saved state, kept in their own list
# so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ModCurveTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/AttenuverterCurveTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/AttenuverterCurveStateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ModCurve/ModCurveBoxTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ModCurve/ModCurvePresetStoreTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/ModCurve/ModCurveEntryPointTests.cpp
)
