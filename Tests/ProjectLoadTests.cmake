# The project-load reveal and staged-load tests (docs/layout/animation.md#project-load-reveal), kept in their own
# list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/App/ProjectOpenProfileTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/LoadGateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Project/DeferredAssetLoadsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ProjectLoad/LoadRevealTimelineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ProjectLoad/ProjectLoadPipelineTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/ProjectLoad/SignalFlowOrderTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Macros/MacroProjectLoadRevealTests.cpp
)
