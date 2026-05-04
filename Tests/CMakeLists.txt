enable_testing()

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(AgentSynthTests
    TestMain.cpp
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
    OllamaProviderTests.cpp
    MainComponentTests.cpp
    GraphEditorTests.cpp
    ModuleComponentTests.cpp
    AIChatComponentTests.cpp
    PolyMidiModuleTests.cpp
    PolySequencerModuleTests.cpp
    AIIntegrationServiceTests.cpp
    ModMatrixTests.cpp
    FXModuleTests.cpp
    VoiceMixerModuleTests.cpp
    EdgeCaseTests.cpp
    IntegrationTests.cpp
    UndoRedoTests.cpp
    E2EWorkflowTests.cpp
    ExternalMidiModuleTests.cpp
    PresetManagerTests.cpp
    ModuleBypassTests.cpp
    VisualSignalFlowTests.cpp
    DistortionSweepTests.cpp
    OscillatorCVModulationTests.cpp
    SettingsWindowTests.cpp
    ShortcutManagerTests.cpp
    AudioRenderingTests.cpp
    ../Source/MainComponent.cpp
    ../Source/UI/GraphEditor.cpp
    ../Source/UI/ModMatrixComponent.cpp
    ../Source/UI/AIChatComponent.cpp
    ../Source/UI/ModuleComponent.cpp
    ../Source/UI/SettingsWindow.cpp
)

target_link_libraries(AgentSynthTests PRIVATE
    GTest::gtest
    AgentSynthCore
)

# Sources are now in AgentSynthCore, so we don't need to add them here manually
# except for the test files themselves.

target_include_directories(AgentSynthTests PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
)

target_compile_definitions(AgentSynthTests PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)

# Link JUCE specifics if needed
target_link_libraries(AgentSynthTests PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_dsp
)

include(GoogleTest)
gtest_discover_tests(AgentSynthTests)