# The headless click audit (idle-to-active edges of the audio modules and sources) and the output-stage tests
# beside it (the bypass/mute transition, the shared Mix and Pan), kept in their own list so Tests/CMakeLists.txt
# stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/AudioRendering/ModuleClickAuditTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/AudioRendering/ModuleBypassMuteTransitionTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ModuleMixPanTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ModulePanAudibleTests.cpp
)
