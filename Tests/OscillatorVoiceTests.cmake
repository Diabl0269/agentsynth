# The Oscillator voice tests, kept in their own list so Tests/CMakeLists.txt stays under the file-size cap.
target_sources(Tests PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorMonoCVInPlaceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorPulseWidthGlideTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorVoiceFeelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorMonoFeelTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorLegatoJumpTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorPolyChordTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorGateRestartTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorFixedRateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorRestartCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/OscillatorAliasTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/WavetableOscillatorModule/WavetableOscillatorModuleGateTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/WavetableOscillatorModule/WavetableOscillatorModuleUnisonCableTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/WavetableOscillatorModule/WavetableOscillatorModuleRestartFadeTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/WavetableOscillatorModule/WavetableOscillatorModuleSyncTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/ADSRLegatoTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/SoftLimitTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/UnisonConstantPowerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/UnisonWidthReferenceTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/UnisonDefaultsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/SingleCableWidthTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/SingleCableWidthLoadTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/UnisonRandLoudnessTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/VoiceCountLatchTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/RenderShortcutsTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Modules/SilentSleepTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/TrackCutTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/TrackJobRunnerTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/TrackWorkerProjectTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/Engine/TrackWorkerWakeProfileTests.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/UI/Graph/GraphEditor/GraphEditorDualIOOscVcaTests.cpp
)
include(${CMAKE_CURRENT_SOURCE_DIR}/ParameterTextTests.cmake) # plain 0..1 amounts shown as percent
include(${CMAKE_CURRENT_SOURCE_DIR}/ModuleHeaderTests.cmake) # the card header buttons
