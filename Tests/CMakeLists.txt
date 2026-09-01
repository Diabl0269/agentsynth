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
    StereoVoiceModuleTests.cpp
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
    AutomationZipperTests.cpp
    MidiKeyboardModuleTests.cpp
    VCAModuleTests.cpp
    LFOModuleTests.cpp
    SampleHoldModuleTests.cpp
    SchmittTriggerTests.cpp
    ComparatorModuleTests.cpp
    SequencerModuleTests.cpp
    TransportServiceTests.cpp
    TimelineDocTests.cpp
    TimelineClipEditingTests.cpp
    TimelineSnapshotTests.cpp
    AutomationKernelTests.cpp
    AutomationApplierTests.cpp
    AutomationRecordTests.cpp
    AutomationSlicingTests.cpp
    TimelineUndoTests.cpp
    TimelineReconciliationTests.cpp
    TimelineValidatorTests.cpp
    TimelineOpsTests.cpp
    TimelineOpsFixtureTests.cpp
    ArrangementContextTests.cpp
    TimelineMidiSourceTests.cpp
    MidiRecorderTests.cpp
    MidiClipFileTests.cpp
    PeaksFileTests.cpp
    RecordTapTests.cpp
    LatencyAlignmentTests.cpp
    AudioClipPlaybackTests.cpp
    HostedPluginTests.cpp
    PluginScanTests.cpp
    HostedPluginEditorWindowTests.cpp
    HostedPluginLaneTests.cpp
    HostedPluginLatencyTests.cpp
    AIStateMapperTests.cpp
    AIPatchValidationTests.cpp
    AIPatchRetryTests.cpp
    AIPatchFixtureReplayTests.cpp
    RequestBudgetTests.cpp
    PatchEvalTests.cpp
    PatchDiffTests.cpp
    AIProviderRegistryTests.cpp
    OllamaProviderTests.cpp
    RemoteProviderTests.cpp
    AuthClientTests.cpp
    AccountServiceTests.cpp
    KeychainTokenStoreTests.cpp
    DeviceIdStoreTests.cpp
    PatchFeedbackStoreTests.cpp
    GeneralFeedbackStoreTests.cpp
    LocalHistoryStoreTests.cpp
    MainComponentTests.cpp
    GraphEditorTests.cpp
    ZoomFrozenCachedImageTests.cpp
    ModuleComponentTests.cpp
    AutomationUiReflectionTests.cpp
    AIChatComponentTests.cpp
    PlanBadgeTests.cpp
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
    RingModulatorModuleTests.cpp
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
    PreferencesSettingsTabTests.cpp
    FeedbackSettingsTabTests.cpp
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
    ModuleLibrarySearchTests.cpp
    ModuleLibraryHelpPopupTests.cpp
    SelectionModelTests.cpp
    SnippetManagerTests.cpp
    MultiSelectTests.cpp
    ClipboardTests.cpp
    PanelAnimationAndLoadingTests.cpp
    TimelinePanelTests.cpp
    TimelinePlayheadTests.cpp
    TimelineTrackHeaderTests.cpp
    TimelineClipLaneTests.cpp
    PianoRollTests.cpp
    MusicalScaleTests.cpp
    NoteColourTests.cpp
    MidiDestinationPickerTests.cpp
    ModuleMidiFlagsTests.cpp
    FocusArbitrationTests.cpp
    AutomationEditorTests.cpp
    TimelineTransportBarTests.cpp
    TimelineViewStateTests.cpp
    MinimapComponentTests.cpp
    PluginProcessorTests.cpp
    AudioEngineTransportTests.cpp
    AudioInputTests.cpp
    AudioInputModuleTests.cpp
    FeedbackGuardTests.cpp
    OfflineTransportDriverTests.cpp
    BounceExporterTests.cpp
    MetronomeTests.cpp
    DeviceChangeTests.cpp
    TimelineE2ETests.cpp
    PatchDocumentTests.cpp
    ProjectBundleTests.cpp
    AutosaveTests.cpp
    AssetManagerTests.cpp
    BrandingTests.cpp
    ../Source/MainComponent.cpp
    ../Source/UI/TimelinePanelComponent.cpp
    ../Source/UI/TimelineRulerComponent.cpp
    ../Source/UI/TimelinePlayheadOverlay.cpp
    ../Source/UI/TimelineTrackHeaderComponent.cpp
    ../Source/UI/TimelineTransportBar.cpp
    ../Source/UI/TimelineClipLaneArea.cpp
    ../Source/UI/PianoRollComponent.cpp
    ../Source/UI/AutomationLaneEditor.cpp
    ../Source/Plugin/PluginProcessor.cpp
    ../Source/Plugin/PluginEditor.cpp
    ../Source/UI/GraphEditor.cpp
    ../Source/UI/ModMatrixComponent.cpp
    ../Source/UI/AIChatComponent.cpp
    ../Source/UI/AccountRow.cpp
    ../Source/UI/PlanBadge.cpp
    ../Source/UI/SignInDialog.cpp
    ../Source/UI/ModuleComponent.cpp
    ../Source/Plugin/Hosting/HostedPluginEditorWindow.cpp
    ../Source/UI/SettingsWindow.cpp
    ../Source/UI/AppearanceSettingsTab.cpp
    ../Source/UI/PreferencesSettingsTab.cpp
    ../Source/UI/ShortcutsSettingsTab.cpp
    ../Source/UI/FeedbackSettingsTab.cpp
)

# MainComponent.cpp above pulls in synth::update::UpdateManager (Source/Update/UpdateManager.h) on
# macOS — Tests compiles MainComponent.cpp directly rather than linking the AgentSynth target, so
# it needs the same Sparkle sources/linking AgentSynth gets in the top-level CMakeLists.txt.
if(APPLE)
    target_sources(Tests PRIVATE ../Source/Update/SparkleUpdateManager.mm)
    set_source_files_properties(../Source/Update/SparkleUpdateManager.mm PROPERTIES COMPILE_OPTIONS -fobjc-arc)
    target_compile_options(Tests PRIVATE "-F${sparkle_SOURCE_DIR}")
    target_link_libraries(Tests PRIVATE "-F${sparkle_SOURCE_DIR}" "-framework Sparkle")

    # Tests is a plain executable (not a .app bundle), always run from the build tree, so — unlike
    # AgentSynth — there's no Contents/Frameworks to embed into; just point rpath straight at
    # Sparkle.framework's fetched location.
    set_target_properties(Tests PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${sparkle_SOURCE_DIR}"
    )
endif()

# Same reasoning as the APPLE block above, for WinSparkle on Windows. A plain hard link is fine
# here (unlike the plugin target in the top-level CMakeLists.txt) — Tests is a normal executable
# with no juce_vst3_helper early-load step, so there's no ordering hazard to work around.
if(WIN32)
    target_compile_definitions(Tests PRIVATE
        SYNTH_UPDATE_FEED_URL_STR=""
        SYNTH_WINSPARKLE_PUBLIC_KEY_STR=""
    )
    target_sources(Tests PRIVATE ../Source/Update/WinSparkleUpdateManager.cpp)
    target_include_directories(Tests PRIVATE "${SYNTH_WINSPARKLE_DIR}/include")
    target_link_libraries(Tests PRIVATE "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.lib")

    add_custom_command(TARGET Tests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${SYNTH_WINSPARKLE_DIR}/x64/Release/WinSparkle.dll"
            "$<TARGET_FILE_DIR:Tests>"
        COMMENT "Copying WinSparkle.dll next to Tests"
    )
endif()

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
    # Same fixture files TimelineOpsHarness replays — one recorded set, asserted here as
    # fast gtest cases so CI covers them without building the harness target.
    TIMELINE_OPS_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tools/TimelineOpsHarness/Fixtures"
    # Real model output recorded once by Tools/AIPatchHarness (--json), replayed offline by
    # AIPatchFixtureReplayTests.cpp — no live model, no network. See
    # Tools/AIPatchHarness/README.md "Recording the fixture corpus".
    AI_PATCH_FIXTURES_DIR="${CMAKE_SOURCE_DIR}/Tests/fixtures/ai-patches"
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