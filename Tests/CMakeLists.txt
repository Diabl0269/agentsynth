enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(GravisynthTests
    TestMain.cpp
    LayoutUtilTests.cpp
    OscillatorTests.cpp
    FilterTests.cpp
    ADSRTests.cpp
    ModuleBaseTests.cpp
    VisualBufferTests.cpp
    AntiClickTests.cpp
    MidiKeyboardModuleTests.cpp
    VCAModuleTests.cpp
    LFOModuleTests.cpp
    SequencerModuleTests.cpp
    AIStateMapperTests.cpp
    AIPatchValidationTests.cpp
    AIPatchRetryTests.cpp
    AIProviderRegistryTests.cpp
    OllamaProviderTests.cpp
    MainComponentTests.cpp
    GraphEditorTests.cpp
    ModuleComponentTests.cpp
    AIChatComponentTests.cpp
    PolyMidiModuleTests.cpp
    PolySequencerModuleTests.cpp
    AIIntegrationServiceTests.cpp
    AIUndoTests.cpp
    ModMatrixTests.cpp
    FXModuleTests.cpp
    VoiceMixerModuleTests.cpp
    EdgeCaseTests.cpp
    IntegrationTests.cpp
    UndoRedoTests.cpp
    E2EWorkflowTests.cpp
    ExternalMidiModuleTests.cpp
    PresetManagerTests.cpp
    SettingsMigrationTests.cpp
    ModuleBypassTests.cpp
    BypassTests.cpp
    VisualSignalFlowTests.cpp
    DistortionSweepTests.cpp
    OscillatorCVModulationTests.cpp
    SettingsWindowTests.cpp
    ShortcutManagerTests.cpp
    ShortcutsSettingsTabTests.cpp
    AudioRenderingTests.cpp
    LogicalPortTests.cpp
    StateRoundTripTests.cpp
    AudioEngineModRoutingTests.cpp
    PerformanceTests.cpp
    ThemeTests.cpp
    IconLibraryTests.cpp
    StatusBarTests.cpp
    FrequencyResponseTests.cpp
    ScopeTests.cpp
    UIAnimationTests.cpp
    GraphEditorOnboardingTests.cpp
    ModuleLibraryComponentTests.cpp
    PanelAnimationAndLoadingTests.cpp
    ../Source/MainComponent.cpp
    ../Source/UI/GraphEditor.cpp
    ../Source/UI/ModMatrixComponent.cpp
    ../Source/UI/AIChatComponent.cpp
    ../Source/UI/ModuleComponent.cpp
    ../Source/UI/SettingsWindow.cpp
    ../Source/UI/AppearanceSettingsTab.cpp
    ../Source/UI/ShortcutsSettingsTab.cpp
)

target_link_libraries(GravisynthTests PRIVATE
    GTest::gtest
    GravisynthCore
)

# Sources are now in GravisynthCore, so we don't need to add them here manually
# except for the test files themselves.

target_include_directories(GravisynthTests PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
)

target_compile_definitions(GravisynthTests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    GRAVISYNTH_THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)

# Link JUCE specifics if needed
target_link_libraries(GravisynthTests PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_dsp
)

include(GoogleTest)
gtest_discover_tests(GravisynthTests)