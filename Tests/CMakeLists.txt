enable_testing()

include(FetchContent)
# URL pinned in cmake/DependencyVersions.cmake (included by the top-level CMakeLists) so the
# CI build/_deps cache key tracks the pin and nothing else.
FetchContent_Declare(
  googletest
  URL ${SYNTH_GOOGLETEST_URL}
)
# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(Tests
    TestMain.cpp
    LayoutUtilTests.cpp
    OscillatorTests.cpp
    FilterTests.cpp
    NoiseModuleTests.cpp
    MathModuleTests.cpp
    MacroControlModuleTests.cpp
    SamplerModuleTests.cpp
    WavetableOscillatorModuleTests.cpp
    WavetableDisplayTests.cpp
    ADSRTests.cpp
    EnvelopeFollowerModuleTests.cpp
    ModuleBaseTests.cpp
    VisualBufferTests.cpp
    AntiClickTests.cpp
    MidiKeyboardModuleTests.cpp
    VCAModuleTests.cpp
    LFOModuleTests.cpp
    SampleHoldModuleTests.cpp
    SequencerModuleTests.cpp
    AIStateMapperTests.cpp
    AIPatchValidationTests.cpp
    AIPatchRetryTests.cpp
    PatchEvalTests.cpp
    AIProviderRegistryTests.cpp
    OllamaProviderTests.cpp
    RemoteProviderTests.cpp
    AuthClientTests.cpp
    AccountServiceTests.cpp
    KeychainTokenStoreTests.cpp
    DeviceIdStoreTests.cpp
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
    PitchShifterModuleTests.cpp
    ParametricEQModuleTests.cpp
    VoiceMixerModuleTests.cpp
    BitcrusherModuleTests.cpp
    NoiseModuleTests.cpp
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
    OutputLevelTests.cpp
    ModuleAdoptionTests.cpp
    StateRoundTripTests.cpp
    CableColourTests.cpp
    AudioEngineModRoutingTests.cpp
    PerformanceTests.cpp
    ThemeTests.cpp
    IconLibraryTests.cpp
    StatusBarTests.cpp
    FrequencyResponseTests.cpp
    EQCurveTests.cpp
    ScopeTests.cpp
    UIAnimationTests.cpp
    GraphEditorOnboardingTests.cpp
    ModuleLibraryComponentTests.cpp
    ModuleLibraryCollapseTests.cpp
    ModuleLibraryCollapseAnimationTests.cpp
    ModuleLibraryScrollTests.cpp
    SelectionModelTests.cpp
    SnippetManagerTests.cpp
    MultiSelectTests.cpp
    ClipboardTests.cpp
    PanelAnimationAndLoadingTests.cpp
    MinimapComponentTests.cpp
    ../Source/MainComponent.cpp
    ../Source/UI/GraphEditor.cpp
    ../Source/UI/ModMatrixComponent.cpp
    ../Source/UI/AIChatComponent.cpp
    ../Source/UI/AccountRow.cpp
    ../Source/UI/SignInDialog.cpp
    ../Source/UI/ModuleComponent.cpp
    ../Source/UI/SettingsWindow.cpp
    ../Source/UI/AppearanceSettingsTab.cpp
    ../Source/UI/ShortcutsSettingsTab.cpp
)

target_link_libraries(Tests PRIVATE
    GTest::gtest
    Core
)

# Sources are now in Core, so we don't need to add them here manually
# except for the test files themselves.

target_include_directories(Tests PRIVATE
    ${CMAKE_SOURCE_DIR}/Source
)

target_compile_definitions(Tests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
)

# Link JUCE specifics if needed
target_link_libraries(Tests PRIVATE
    juce::juce_core
    juce::juce_events
    juce::juce_audio_basics
    juce::juce_dsp
)

include(GoogleTest)
# The post-build discovery step runs the binary with --gtest_list_tests. CMake's default budget for
# that is 5 s, which the suite outgrew somewhere past ~1250 tests: discovery times out, the build
# fails, and make deletes the just-linked binary. Raise the budget rather than chase the threshold.
gtest_discover_tests(Tests DISCOVERY_TIMEOUT 120)