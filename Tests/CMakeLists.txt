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
    UI/Layout/LayoutUtilTests.cpp
    Modules/OscillatorTests.cpp
    Modules/FilterTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModulePanLawTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleOscillatorTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleFilterTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleSplitBlockTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleVCATests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleDefaultPresetTests.cpp
    Modules/StereoVoiceModule/StereoVoiceModuleDeclarationTests.cpp
    Modules/NoiseModuleTests.cpp
    Modules/MathModuleTests.cpp
    Modules/MacroControlModuleTests.cpp
    Modules/SamplerModuleTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModulePortsTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleOutputTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleCVTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleImportTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleStateTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleWarpTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleStereoTests.cpp
    Modules/WavetableOscillatorModule/WavetableOscillatorModuleVoiceTests.cpp
    UI/ModuleViews/WavetableDisplayTests.cpp
    Modules/ADSR/ADSRStageTests.cpp
    Modules/ADSR/ADSRGateTests.cpp
    Modules/ADSR/ADSRPolyTests.cpp
    Modules/ADSR/ADSRThresholdTests.cpp
    Modules/ADSR/ADSRClickDiagnosticsTests.cpp
    Modules/ADSR/ADSRReadoutFormattingTests.cpp
    Modules/Envelope/EnvelopeGeneratorTests.cpp
    Modules/EnvelopeFollowerModuleTests.cpp
    Modules/ModuleBaseTests.cpp
    Engine/VisualBufferTests.cpp
    Modules/AntiClickTests.cpp
    Timeline/AutomationZipperTests.cpp
    Modules/MidiKeyboardModuleTests.cpp
    Modules/VCAModuleTests.cpp
    Modules/LFOModuleTests.cpp
    Modules/SampleHoldModuleTests.cpp
    Modules/SchmittTriggerTests.cpp
    Modules/ComparatorModuleTests.cpp
    Modules/SequencerModuleTests.cpp
    Engine/TransportServiceTests.cpp
    Timeline/TimelineDoc/TimelineDocCoreTests.cpp
    Timeline/TimelineDoc/TimelineDocTracksTests.cpp
    Timeline/TimelineDoc/TimelineDocClipsTests.cpp
    Timeline/TimelineDoc/TimelineDocNotesTests.cpp
    Timeline/TimelineDoc/TimelineDocAutomationTests.cpp
    Timeline/TimelineDoc/TimelineDocSerializationTests.cpp
    Timeline/TimelineDoc/TimelineDocMarkersTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingNotesTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingQuantiseTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingSplitJoinTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingSerializationTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingToolsTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingDragTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingContextTests.cpp
    Timeline/TimelineClipEditing/TimelineClipEditingGridHierarchyTests.cpp
    Timeline/TimelineSnapshotTests.cpp
    Timeline/AutomationKernelTests.cpp
    Timeline/AutomationApplierTests.cpp
    Timeline/AutomationRecordTests.cpp
    Timeline/AutomationSlicingTests.cpp
    Timeline/TimelineUndoTests.cpp
    Timeline/TimelineReconciliationTests.cpp
    Timeline/TimelineValidatorTests.cpp
    Timeline/TimelineOpsTests.cpp
    Timeline/TimelineOpsFixtureTests.cpp
    Timeline/ArrangementContextTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceEmissionTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceHeldNoteTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceMuteSoloTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceBindingTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceLoopTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceEngineTests.cpp
    Timeline/TimelineMidiSource/TimelineMidiSourceAuditionTests.cpp
    Timeline/MidiRecorderTests.cpp
    Timeline/MidiClipFileTests.cpp
    Timeline/PeaksFileTests.cpp
    Timeline/RecordTapTests.cpp
    Mixer/ChannelStripTests.cpp
    Mixer/MixerSoloTests.cpp
    Mixer/ChannelFlow/ChannelFlowTests.cpp
    Mixer/ChannelFlow/ChannelFlowInstrumentTests.cpp
    Mixer/ChannelFlow/ChannelFlowPluginInstrumentTests.cpp
    Mixer/ChannelFlow/ChannelFlowAutoChannelTests.cpp
    Mixer/ChannelFlow/ChannelFlowCreateChannelsTests.cpp
    Mixer/ChannelFlow/ChannelFlowMakeChannelCoreTests.cpp
    Mixer/ChannelFlow/ChannelFlowMakeChannelAppTests.cpp
    Mixer/ChannelFlow/ChannelFlowTrackChannelLinkCoreTests.cpp
    Mixer/ChannelFlow/ChannelFlowTrackChannelLinkTests.cpp
    Mixer/TrackPreset/TrackPresetTests.cpp
    Mixer/TrackPreset/TrackPresetCaptureTests.cpp
    Mixer/TrackPreset/TrackPresetDefaultsTests.cpp
    Mixer/MixerModel/MixerModelColumnTests.cpp
    Mixer/MixerModel/MixerModelInsertTests.cpp
    Mixer/MixerModel/MixerModelBusColumnTests.cpp
    Mixer/Sends/MixerSendLevelTests.cpp
    Mixer/Sends/MixerSendFlowTests.cpp
    Mixer/Sends/MixerBusSoloTests.cpp
    Mixer/Sends/MixerSendLatencyTests.cpp
    Engine/LatencyAlignmentTests.cpp
    Timeline/AudioClipPlaybackTests.cpp
    Plugin/HostedPluginTests.cpp
    Plugin/PluginScan/PluginScanLifecycleTests.cpp
    Plugin/PluginScan/PluginScanCrashTimeoutTests.cpp
    Plugin/PluginScan/PluginScanCrashGuardTests.cpp
    Plugin/PluginScan/PluginScanBackendTests.cpp
    Plugin/PluginScan/PluginScanChildProcessTests.cpp
    Plugin/PluginScan/PluginScanLibraryTests.cpp
    Plugin/PluginScan/PluginScanPersistenceTests.cpp
    Plugin/HostedPluginEditorWindowTests.cpp
    Plugin/HostedPluginLaneTests.cpp
    Plugin/HostedPluginLatencyTests.cpp
    AI/AIStateMapper/AIStateMapperSnapshotsTests.cpp
    AI/AIStateMapper/AIStateMapperSchemaTests.cpp
    AI/AIStateMapper/AIStateMapperValidationTests.cpp
    AI/AIStateMapper/AIStateMapperAutoConnectTests.cpp
    AI/AIStateMapper/AIStateMapperPatchFormatTests.cpp
    AI/AIPatchValidationTests.cpp
    AI/AIPatchRetryTests.cpp
    AI/AIPatchFixtureReplayTests.cpp
    AI/RequestBudgetTests.cpp
    AI/PatchEvalTests.cpp
    AI/PatchDiffTests.cpp
    AI/AIProviderRegistryTests.cpp
    AI/OllamaProvider/OllamaProviderRequestTests.cpp
    AI/OllamaProvider/OllamaProviderLifecycleTests.cpp
    AI/OllamaProvider/OllamaProviderErrorMappingTests.cpp
    AI/OllamaProvider/OllamaProviderCancellationTests.cpp
    AI/RemoteProvider/RemoteProviderRequestShapeTests.cpp
    AI/RemoteProvider/RemoteProviderResponseHandlingTests.cpp
    AI/RemoteProvider/RemoteProviderTransportTests.cpp
    AI/RemoteProvider/RemoteProviderCapabilityTests.cpp
    Account/AuthClient/AuthClientDeviceAuthTests.cpp
    Account/AuthClient/AuthClientAccountTests.cpp
    Account/AuthClient/AuthClientConversationsTests.cpp
    Account/AuthClient/AuthClientFeedbackTests.cpp
    Account/AccountServiceTests.cpp
    Account/KeychainTokenStoreTests.cpp
    Account/DeviceIdStoreTests.cpp
    AI/PatchFeedbackStoreTests.cpp
    Account/GeneralFeedbackStoreTests.cpp
    AI/LocalHistoryStoreTests.cpp
    App/MainComponent/MainComponentStartupPanelsTests.cpp
    App/MainComponent/MainComponentLayoutTests.cpp
    App/MainComponent/MainComponentDocumentTests.cpp
    App/MainComponent/MainComponentUnsavedGuardTests.cpp
    App/MainComponent/MainComponentProviderMiscTests.cpp
    App/MainComponent/MainComponentCommandTableTests.cpp
    UI/Chrome/WelcomeScreenTests.cpp
    UI/Graph/GraphEditor/GraphEditorTests.cpp
    UI/Graph/GraphEditor/GraphEditorLayoutTests.cpp
    UI/Graph/GraphEditor/GraphEditorPolyLinkTests.cpp
    UI/Graph/GraphEditor/GraphEditorDualIOTests.cpp
    UI/Graph/GraphEditor/GraphEditorIntegrationTests.cpp
    UI/Graph/GraphEditor/GraphEditorViewportTests.cpp
    UI/Graph/GraphEditor/GraphEditorSmartConnectionTests.cpp
    UI/Graph/GraphEditor/GraphEditorSmartConnectionInsertTests.cpp
    UI/Graph/GraphEditor/GraphEditorSmartConnectionPreviewTests.cpp
    UI/Graph/GraphEditor/GraphEditorSmartConnectionMatrixTests.cpp
    UI/Graph/GraphEditor/GraphEditorNodeActionsTests.cpp
    UI/Graph/SmartConnectionEngine/SmartConnectionEngineTests.cpp
    UI/Graph/GraphDragDropController/GraphDragDropControllerTests.cpp
    UI/Layout/ZoomFrozenCachedImageTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentLayoutTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentPortsTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentInteractionTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentAudioDropTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentMacroBankTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentWavetableTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentPaintTests.cpp
    UI/Graph/ModuleComponent/ModuleComponentEnvelopeCardTests.cpp
    UI/Graph/AutomationUiReflectionTests.cpp
    UI/Assistant/AIChatComponent/AIChatComponentTests.cpp
    UI/Assistant/AIChatComponent/AIChatComponentHistoryTests.cpp
    UI/Assistant/AIChatComponent/AIChatComponentArrangeTests.cpp
    UI/Assistant/PlanBadgeTests.cpp
    Modules/PolyMidiModuleTests.cpp
    Modules/PolySequencerModuleTests.cpp
    AI/AIIntegrationService/AIIntegrationServiceProviderConfigTests.cpp
    AI/AIIntegrationService/AIIntegrationServiceHistoryTests.cpp
    AI/AIIntegrationService/AIIntegrationServicePatchApplyTests.cpp
    AI/AIIntegrationService/AIIntegrationServiceSystemPromptTests.cpp
    AI/AIIntegrationService/AIIntegrationServiceTimelineToolsTests.cpp
    AI/AIIntegrationService/AIIntegrationServiceArrangeModeTests.cpp
    AI/AIUndoTests.cpp
    UI/Graph/ModMatrixTests.cpp
    FX/FXModule/FXModuleAttenuverterTests.cpp
    FX/FXModule/FXModuleBitcrusherTests.cpp
    FX/FXModule/FXModuleChorusTests.cpp
    FX/FXModule/FXModuleCompressorTests.cpp
    FX/FXModule/FXModuleDelayTests.cpp
    FX/FXModule/FXModuleDistortionTests.cpp
    FX/FXModule/FXModuleFlangerTests.cpp
    FX/FXModule/FXModuleLimiterTests.cpp
    FX/FXModule/FXModulePhaserTests.cpp
    FX/FXModule/FXModulePortLabelsTests.cpp
    FX/FXModule/FXModuleReverbTests.cpp
    FX/GateModuleTests.cpp
    FX/PitchShifterModuleTests.cpp
    FX/ParametricEQModuleTests.cpp
    Modules/VoiceMixerModuleTests.cpp
    FX/BitcrusherModuleTests.cpp
    FX/RingModulatorModuleTests.cpp
    FX/RingModulatorGainTests.cpp
    Modules/NoiseModuleTests.cpp
    Modules/EdgeCaseTests.cpp
    Engine/IntegrationTests.cpp
    Project/UndoRedo/UndoRedoIdentityPreservingTests.cpp
    Project/UndoRedo/UndoRedoBasicOpsTests.cpp
    Project/UndoRedo/UndoRedoScenarioTests.cpp
    Project/UndoRedo/UndoRedoEditSerialTests.cpp
    App/E2EWorkflowTests.cpp
    Modules/ExternalMidiModuleTests.cpp
    Project/PresetManagerTests.cpp
    Project/SettingsMigrationTests.cpp
    Modules/ModuleBypassTests.cpp
    Modules/BypassTests.cpp
    Engine/VisualSignalFlowTests.cpp
    FX/DistortionSweepTests.cpp
    Modules/OscillatorCVModulationTests.cpp
    UI/Settings/SettingsWindowTests.cpp
    App/ShortcutManager/ShortcutManagerActionRegressionTests.cpp
    App/ShortcutManager/ShortcutManagerApiTests.cpp
    App/ShortcutManager/ShortcutManagerConflictTests.cpp
    App/ShortcutManager/ShortcutManagerDefaultsTests.cpp
    App/ShortcutManager/ShortcutManagerKeyMatchingTests.cpp
    UI/Settings/ShortcutsSettingsTabTests.cpp
    UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabGraphBehaviourTests.cpp
    UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabTimelineBehaviourTests.cpp
    UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabAutosaveTests.cpp
    UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabLifecycleTests.cpp
    UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabMixerPlacementTests.cpp
    UI/Settings/FeedbackSettingsTabTests.cpp
    Engine/AudioRendering/AudioRenderingBasicTests.cpp
    Engine/AudioRendering/AudioRenderingFXModuleTests.cpp
    Engine/AudioRendering/AudioRenderingModulationTests.cpp
    Engine/AudioRendering/AudioRenderingPresetTests.cpp
    Engine/AudioRendering/AudioRenderingSnapshotTests.cpp
    Engine/GainStaging/ModuleGainAuditTests.cpp
    Engine/GainStaging/MixAudibilityTests.cpp
    Engine/LogicalPortTests.cpp
    Engine/OutputLevelTests.cpp
    Modules/ModuleAdoptionTests.cpp
    Project/StateRoundTripTests.cpp
    UI/Graph/CableColourTests.cpp
    Engine/AudioEngineModRoutingTests.cpp
    Engine/PerformanceTests.cpp
    UI/Theme/ThemeTests.cpp
    UI/Theme/IconLibraryTests.cpp
    UI/Chrome/StatusBarTests.cpp
    UI/ModuleViews/FrequencyResponseTests.cpp
    UI/ModuleViews/EQCurveTests.cpp
    UI/ModuleViews/ScopeTests.cpp
    UI/ModuleViews/CurveEditor/CurveModelTests.cpp
    UI/ModuleViews/CurveEditor/CurveEditorGeometryTests.cpp
    UI/ModuleViews/CurveEditor/CurveEditorInteractionTests.cpp
    UI/ModuleViews/CurveEditor/CurveEditorPaintTests.cpp
    UI/Layout/UIAnimationTests.cpp
    UI/Graph/GraphEditorOnboardingTests.cpp
    UI/Library/ModuleLibraryComponentTests.cpp
    UI/Library/ModuleLibraryCollapseTests.cpp
    UI/Library/ModuleLibraryCollapseAnimationTests.cpp
    UI/Library/ModuleLibraryScrollTests.cpp
    UI/Library/ModuleLibrarySearchTests.cpp
    UI/Library/ModuleLibraryHelpPopupTests.cpp
    UI/Library/ModuleLibraryKeyboardNavTests.cpp
    UI/Graph/SelectionModelTests.cpp
    Project/SnippetManager/SnippetManagerExtractTests.cpp
    Project/SnippetManager/SnippetManagerPrepareInsertTests.cpp
    Project/SnippetManager/SnippetManagerMacroTests.cpp
    Project/SnippetManager/SnippetManagerPersistenceTests.cpp
    UI/Graph/MultiSelectTests.cpp
    Macros/MacroGroupController/MacroGroupControllerTests.cpp
    Macros/MacroContainer/MacroContainerTests.cpp
    Macros/MacroContainer/MacroContainerGeometryTests.cpp
    Macros/MacroContainer/MacroContainerInteractionTests.cpp
    Macros/MacroSetTests.cpp
    Macros/MacroPortModuleTests.cpp
    Macros/MacroPortConfigDialogTests.cpp
    Macros/MacroPortFlow/MacroPortFlowAddTests.cpp
    Macros/MacroPortFlow/MacroPortFlowEditTests.cpp
    Macros/MacroPortFlow/MacroPortFlowShapeChangeTests.cpp
    Macros/MacroPortFlow/MacroPortFlowCableDropTests.cpp
    Macros/MacroPortFlow/MacroPortFlowAutoCreateOnDragTests.cpp
    Macros/MacroPortRealMouseDragTests.cpp
    UI/Graph/DragStateResetTests.cpp
    UI/Graph/DragStateAsyncRebuildTests.cpp
    Macros/MacroAutoPort/MacroAutoPortCreationTests.cpp
    Macros/MacroAutoPort/MacroAutoPortUngroupTests.cpp
    Macros/MacroAutoPort/MacroAutoPortDeleteTests.cpp
    Macros/MacroCardJackTests.cpp
    Macros/MacroPortWidgetTests.cpp
    Macros/MacroBypassMuteTests.cpp
    UI/Graph/ClipboardTests.cpp
    UI/Layout/PanelAnimationAndLoadingTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelRulerTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelMarkerTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelZoomSnapScrollTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelTrackHeaderTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelAppWiringTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelResizeTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelAuthoringGestureTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelToolStripTests.cpp
    UI/Timeline/TimelinePanel/TimelinePanelFollowPlayheadTests.cpp
    UI/Timeline/TimelinePlayheadTests.cpp
    UI/Timeline/TimelineTrackHeaderTests.cpp
    UI/Timeline/TimelineTrackHeaderContextMenuTests.cpp
    UI/Timeline/TimelineTrackFocusTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLaneAreaTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLaneAuthoringTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLaneEditToolsTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLaneMouseTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLanePaintingTests.cpp
    UI/Timeline/TimelineClipLane/TimelineClipLaneSelectionTests.cpp
    UI/PianoRoll/NoteSelectionModelTests.cpp
    UI/PianoRoll/NoteHitTestMarqueeTests.cpp
    UI/PianoRoll/PianoRollComponentTests.cpp
    UI/PianoRoll/PianoRollEditToolsTests.cpp
    UI/PianoRoll/PianoRollClipboardTests.cpp
    UI/PianoRoll/PianoRollArrowKeyTests.cpp
    UI/PianoRoll/PianoRollShortcutsTests.cpp
    UI/PianoRoll/PianoRollZoomTests.cpp
    UI/PianoRoll/PianoRollPaintingTests.cpp
    UI/PianoRoll/PianoRollScaleAssistTests.cpp
    UI/PianoRoll/PianoRollHeaderChipsTests.cpp
    UI/PianoRoll/PianoRollAuditionTests.cpp
    UI/PianoRoll/PianoRollMouseTests.cpp
    Timeline/MusicalScaleTests.cpp
    UI/PianoRoll/NoteColourTests.cpp
    UI/Timeline/MidiDestinationPickerTests.cpp
    Modules/ModuleMidiFlagsTests.cpp
    App/FocusArbitration/FocusArbitrationClipboardTests.cpp
    App/FocusArbitration/FocusArbitrationSelectionTests.cpp
    App/FocusArbitration/FocusArbitrationPlaybackDeleteTests.cpp
    App/FocusArbitration/FocusArbitrationZoomGridTests.cpp
    App/FocusArbitration/FocusArbitrationShiftedKeysTests.cpp
    UI/Layout/FocusRegionTests.cpp
    UI/Timeline/AutomationEditorTests.cpp
    UI/Timeline/TimelineTransportBarTests.cpp
    UI/Timeline/TimelineViewStateTests.cpp
    UI/Mixer/MixerPanelComponentTests.cpp
    UI/Mixer/MixerPanelUndoUnbindTests.cpp
    UI/Mixer/MixerFaderTests.cpp
    UI/Mixer/MixerSendListTests.cpp
    UI/Mixer/MixerDockComponentTests.cpp
    UI/Mixer/MixerEqThumbnailTests.cpp
    UI/Mixer/MixerColumnComponentTests.cpp
    UI/Mixer/MixerInsertListTests.cpp
    UI/Mixer/MixerPlacementControllerTests.cpp
    UI/Layout/DetachablePanelHost/DetachablePanelHostTests.cpp
    UI/Layout/DetachablePanelHost/DetachedPanelWindowTests.cpp
    UI/Layout/DetachablePanelHost/DetachRedockStateTests.cpp
    UI/Mixer/MixerPanelKeyboardFocusTests.cpp
    UI/Mixer/MixerAccessibilityTests.cpp
    UI/Mixer/MixerFocusRegionTests.cpp
    UI/Graph/MinimapComponentTests.cpp
    Plugin/PluginProcessorTests.cpp
    Engine/AudioEngineTransportTests.cpp
    Engine/AudioInputTests.cpp
    Modules/AudioInputModuleTests.cpp
    Engine/FeedbackGuardTests.cpp
    Engine/OfflineTransportDriverTests.cpp
    Engine/BounceExporter/BounceExporterCancellationTests.cpp
    Engine/BounceExporter/BounceExporterCorrectnessTests.cpp
    Engine/BounceExporter/BounceExporterFailureTests.cpp
    Engine/BounceExporter/BounceExporterFormatTests.cpp
    Engine/BounceExporter/BounceExporterLoopRangeTests.cpp
    Engine/BounceExporter/BounceExporterProgressTests.cpp
    Engine/BounceExporter/BounceExporterStreamedClipTests.cpp
    Engine/BounceExporter/BounceExporterTransportTests.cpp
    Engine/BounceRunnerTests.cpp
    Engine/StemExportTests.cpp
    Engine/StemExportNamingTests.cpp
    Engine/StemRunnerTests.cpp
    UI/Chrome/ExportAudioDialogTests.cpp
    Engine/MetronomeTests.cpp
    Engine/DeviceChangeTests.cpp
    Timeline/TimelineE2ETests.cpp
    Project/PatchDocumentTests.cpp
    Project/ProjectBundleTests.cpp
    Project/AutosaveTests.cpp
    Project/RecentProjectsTests.cpp
    Project/AssetManagerTests.cpp
    App/BrandingTests.cpp
    ../Source/MainComponent/MainComponent.cpp
    ../Source/MainComponent/MainComponentSetup.cpp
    ../Source/MainComponent/MainComponentSetupToolbar.cpp
    ../Source/MainComponent/MainComponentSetupTimeline.cpp
    ../Source/MainComponent/MainComponentCallbacks.cpp
    ../Source/MainComponent/MainComponentFileIO.cpp
    ../Source/MainComponent/MainComponentCommands.cpp
    ../Source/MainComponent/MainComponentCommandTable.cpp
    ../Source/MainComponent/MainComponentPanels.cpp
    ../Source/MainComponent/MainComponentTimeline.cpp
    ../Source/MainComponent/MainComponentTrackHeaderHost.cpp
    ../Source/MainComponent/MainComponentTrackCreation.cpp
    ../Source/MainComponent/MainComponentTrackPresets.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelStrips.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelClipClipboard.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelShortcuts.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelTrackHeaders.cpp
    ../Source/UI/Timeline/TimelinePanelComponent/TimelinePanelLayout.cpp
    ../Source/UI/Timeline/TimelineRulerComponent.cpp
    ../Source/UI/Timeline/TimelinePlayheadOverlay.cpp
    ../Source/UI/Timeline/TimelineTrackHeaderComponent.cpp
    ../Source/UI/Timeline/ChannelChipComponent.cpp
    ../Source/UI/Timeline/TrackChannelLinkController.cpp
    ../Source/UI/Timeline/TimelineTransportBar.cpp
    ../Source/UI/Mixer/MixerMeter.cpp
    ../Source/UI/Mixer/MixerFader.cpp
    ../Source/UI/Mixer/MixerInsertList.cpp
    ../Source/UI/Mixer/MixerEqThumbnail.cpp
    ../Source/UI/Mixer/MixerSendList.cpp
    ../Source/UI/Mixer/MixerColumnComponent.cpp
    ../Source/UI/Mixer/MixerDirectColumn.cpp
    ../Source/UI/Mixer/MixerMasterColumn.cpp
    ../Source/UI/Mixer/MixerPanelComponent/MixerPanelComponent.cpp
    ../Source/UI/Mixer/MixerPanelComponent/MixerPanelKeyboard.cpp
    ../Source/UI/Mixer/MixerDockComponent.cpp
    ../Source/UI/Mixer/MixerPlacementController.cpp
    ../Source/UI/Layout/DetachablePanelHost/DetachablePanelHost.cpp
    ../Source/UI/Layout/DetachablePanelHost/DetachedPanelWindow.cpp
    ../Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneArea.cpp
    ../Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLanePainting.cpp
    ../Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneMouse.cpp
    ../Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneSelection.cpp
    ../Source/UI/Timeline/TimelineClipLaneArea/TimelineClipLaneEditTools.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollComponent.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollScaleAssist.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollPainting.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollEditTools.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollAudition.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollClipboardAndKeys.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollMouse.cpp
    ../Source/UI/PianoRoll/PianoRollComponent/PianoRollZoom.cpp
    ../Source/UI/Timeline/AutomationLaneEditor.cpp
    ../Source/Plugin/PluginProcessor.cpp
    ../Source/Plugin/PluginEditor.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditor.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorCables.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorConnections.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorModuleTitles.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorSmartConnections.cpp
    ../Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngine.cpp
    ../Source/UI/Graph/SmartConnectionEngine/SmartConnectionEngineApply.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorCanvas.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorSelection.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorMacroCards.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorMacroPrompts.cpp
    ../Source/UI/Graph/MacroGroupController/MacroGroupController.h
    ../Source/UI/Graph/MacroGroupController/MacroGroupControllerGeometry.cpp
    ../Source/UI/Graph/MacroGroupController/MacroGroupControllerGrouping.cpp
    ../Source/UI/Graph/MacroGroupController/MacroGroupControllerBypassMute.cpp
    ../Source/UI/Graph/MacroGroupController/MacroGroupControllerPortSplice.cpp
    ../Source/UI/Graph/MacroGroupController/MacroGroupControllerPorts.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorChannels.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorCommands.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorDragDrop.cpp
    ../Source/UI/Graph/GraphDragDropController/GraphDragDropController.h
    ../Source/UI/Graph/GraphDragDropController/GraphDragDropController.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorStereoWiring.cpp
    ../Source/UI/Graph/GraphEditor/GraphEditorPersistence.cpp
    ../Source/UI/Chrome/ExportAudioDialog.cpp
    ../Source/UI/Graph/ModMatrixComponent.cpp
    ../Source/UI/Assistant/AIChatComponent/AIChatComponent.cpp
    ../Source/UI/Assistant/AIChatComponent/AIChatComponentMessageList.cpp
    ../Source/UI/Assistant/AIChatComponent/AIChatComponentSending.cpp
    ../Source/UI/Assistant/AIChatComponent/AIChatComponentProvider.cpp
    ../Source/UI/Assistant/AIChatComponent/AIChatComponentHistory.cpp
    ../Source/UI/Assistant/AccountRow.cpp
    ../Source/UI/Assistant/PlanBadge.cpp
    ../Source/UI/Assistant/SignInDialog.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponent.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentEQCard.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentAudioDrop.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentWavetable.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentLayout.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentPaint.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentInteraction.cpp
    ../Source/UI/Graph/ModuleComponent/ModuleComponentEnvelopeCard.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibrarySearch.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryRows.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryCollapse.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryLayout.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryPainting.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryInput.cpp
    ../Source/UI/Library/ModuleLibraryComponent/ModuleLibraryHelpPopover.cpp
    ../Source/UI/Macros/MacroCardComponent.cpp
    ../Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogLifecycle.cpp
    ../Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogRowOrdering.cpp
    ../Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogTestSeams.cpp
    ../Source/UI/Macros/MacroPortConfigDialog/MacroPortConfigDialogAutoPortPrompt.cpp
    ../Source/Plugin/Hosting/HostedPluginEditorWindow.cpp
    ../Source/UI/Settings/SettingsWindow.cpp
    ../Source/UI/Settings/AppearanceSettingsTab.cpp
    ../Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabLifecycle.cpp
    ../Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabGraphBehaviour.cpp
    ../Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabTimelineBehaviour.cpp
    ../Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabAutosave.cpp
    ../Source/UI/Settings/PreferencesSettingsTab/PreferencesSettingsTabMixerDefaults.cpp
    ../Source/UI/Settings/ShortcutsSettingsTab.cpp
    ../Source/UI/Settings/FeedbackSettingsTab.cpp
    ../Source/UI/Chrome/WelcomeScreenComponent.cpp
    ../Source/UI/ModuleViews/CurveEditor/CurveModel.cpp
    ../Source/UI/ModuleViews/CurveEditor/CurveEditorGeometry.cpp
    ../Source/UI/ModuleViews/CurveEditor/CurveEditorComponent.cpp
    ../Source/UI/ModuleViews/CurveEditor/CurveEditorPaint.cpp
)

# MainComponent.h (included by the MainComponent/*.cpp units above) pulls in
# synth::update::UpdateManager (Source/Update/UpdateManager.h) on macOS — Tests compiles those
# units directly rather than linking the AgentSynth target, so it needs the same Sparkle
# sources/linking AgentSynth gets in the top-level CMakeLists.txt.
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
    # WhatsNewData.h (generated at configure time by the root CMakeLists.txt's "What's New"
    # block) — Tests compiles the MainComponent/*.cpp units directly (see the comment above), so
    # it needs the same generated-header include path AppUI gets.
    ${SYNTH_WHATSNEW_GENERATED_DIR}
)

target_compile_definitions(Tests PRIVATE
    JUCE_MODAL_LOOPS_PERMITTED=1
    THEMES_DIR="${CMAKE_SOURCE_DIR}/themes"
    # Tests/ itself, for data resolved relative to the test tree (Tests/reference/) — a test file's own
    # __FILE__ depth changes whenever tests are reorganized into area directories.
    TESTS_ROOT_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
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