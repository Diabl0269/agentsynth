# The Parametric EQ (docs/modules/fx-modules.md#parametric-eq-module): engine, CV, audibility, the saved model
# and the goldens of what the AI sees, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQModuleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQCVTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQAudibleTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQSavedModelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQResponseTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/FX/ParametricEQ/ParametricEQHiddenSurfaceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/AI/AIStateMapper/AIStateMapperEqBandsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ModuleComponent/ModuleComponentEqBandRowTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/EQCurveBandsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/ModuleViews/EQCurveShortcutTests.cpp
)
