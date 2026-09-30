#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AccountService.h"
#include "AppUndoManager.h"
#include "Branding.h"
#include "MainComponentCollectArchiveSeams.h"
#include "MainComponentExportMidiSeams.h"
#include "MainComponentRemoteActionInvoker.h"
#include "MainComponentTypes.h"
#include "MidiRemote/MidiLearnController.h"
#include "MidiRemote/MidiRemoteFeedbackOutputs.h"
#include "MidiRemote/RemoteEngine/RemoteEngine.h"
#include "MidiRemote/RemoteModel.h"
#include "Mixer/TrackPresetManager.h"
#include "Modules/RecordTapModule.h"
#include "Plugin/Hosting/HostedPluginWindowManager.h"
#include "Plugin/Hosting/PluginCardLayoutStore.h"
#include "Plugin/Hosting/PluginScanService.h"
#include "PresetManager.h"
#include "ProjectBundle.h"
#include "RecentProjects.h"
#include "ShortcutManager/ShortcutManager.h"
#include "SnippetManager.h"
#include "Timeline/AutomationRecorder.h"
#include "Timeline/MidiRecorder.h"
#include "Timeline/TimelineDoc/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include "Transport/BounceRunner.h"
#include "Transport/StemExporter.h"
#include "Transport/StemRunner.h"
#include "Transport/TransportDoc.h"
#include "Transport/TransportNudge.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Chrome/ToolbarComponent.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Layout/BottomDockComponent.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Mixer/MixerPlacementController.h"
#include "UI/Theme/AppLookAndFeel/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
#include "UI/Timeline/TrackChannelLinkController.h"
#include "Update/UpdateManager.h"
#include "UserSettings.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>
#include <optional>
#include <vector>

class AudioEngine;
class GraphEditor;
namespace synth {
struct CollectResult; // Project/ProjectCollector.h
}
class MainComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::Timer
    , public juce::ApplicationCommandTarget
    , private juce::ChangeListener
    , private synth::AIIntegrationService::Listener
    , private synth::TimelineDoc::Listener
    , private synth::ui::TrackHeaderHost
    , private juce::FocusChangeListener
    , private synth::PluginScanService::Listener {
public:
    /** Primary ctor: ThemeManager/LookAndFeel injected; provider nullptr = the saved pref. Owns its
     *  own (standalone) AudioEngine. */
    MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                  std::unique_ptr<synth::AIProvider> provider = nullptr);

    /** Plugin ctor: `externalEngine` is owned by the processor and must outlive this; its lifecycle is never touched.
     */
    MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf, AudioEngine& externalEngine,
                  std::unique_ptr<synth::AIProvider> provider = nullptr);

    /** Delegating ctor for tests/legacy call sites: lazily owns default ThemeManager + AppLookAndFeel. */
    explicit MainComponent(std::unique_ptr<synth::AIProvider> provider = nullptr,
                           synth::AIProviderRegistry registry = synth::AIProviderRegistry::createDefault(),
                           synth::ControllerProfileStore profileStore = controllerProfileStoreForCtor());

    ~MainComponent() override;

    static synth::ControllerProfileStore controllerProfileStoreForCtor(); // real folder, or the test override
    static void setControllerProfileTestDirectory(const juce::File& dir); // test-only

    void timerCallback() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    bool keyPressed(const juce::KeyPress& key) override;

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    void updateCommandShortcuts();

    using EditSurface = synth::maincomponent::EditSurface;
    using SlidingPanel = synth::maincomponent::SlidingPanel;
    using UnsavedChangesChoice = synth::maincomponent::UnsavedChangesChoice;
    using AutosaveRecoveryChoice = synth::maincomponent::AutosaveRecoveryChoice;
    using PatchLoadMode = synth::maincomponent::PatchLoadMode;

    EditSurface resolveEditSurface() const;
    /** The component the open-context-menu action starts from: the real keyboard focus. */
    juce::Component* getFocusedComponentForMenu() const;

    bool performRepeatSelection(int count);

    static constexpr int kMinRepeatCount = 1; // Repeat's count bounds (dialog clamp + performRepeatSelection).
    static constexpr int kMaxRepeatCount = 64;

    /** "remote" for a brand-new install, else "ollama". */
    static juce::String resolveDefaultProviderId(bool hasExistingSettingsFile);

    /** The Preferences "Natural scrolling" key. DEFAULT TRUE. */
    static constexpr const char* kNaturalScrollingKey = "naturalScrolling";
    /** Re-reads kNaturalScrollingKey and pushes it into the timeline panel + piano roll; idempotent. */
    void applyNaturalScrollingPreference();

    /** The "Scroll up to zoom in" key. DEFAULT TRUE. */
    static constexpr const char* kZoomScrollUpZoomsInKey = "zoomScrollUpZoomsIn";
    /** Re-reads kZoomScrollUpZoomsInKey and pushes it into the timeline panel; idempotent. */
    void applyZoomScrollPreference();

    /** Re-reads the two MIDI Remote preferences and pushes them into the engine / badge painter; idempotent. */
    void applyMidiRemotePreferences();

    /** Per-press zoom step for the four zoom commands; the out factor is the exact reciprocal. */
    static constexpr double kZoomInFactor = 1.25;
    static constexpr double kZoomOutFactor = 1.0 / kZoomInFactor;

    synth::ui::TimelinePanelComponent& getTimelinePanel() { return timelinePanel; }
    synth::ui::BottomDockComponent& getBottomDock() { return bottomDock; }
    /** The real construction site calls setCreatesNativeWindows(true) on this. */
    synth::HostedPluginWindowManager& getPluginWindowManager() { return pluginWindowManager; }
    /** Opens the dock if hidden (never closes it) and switches to `tab`; a detached tab's window is raised instead. */
    void showBottomDockTab(synth::ui::BottomDockComponent::Tab tab);
    /** The open half of showBottomDockTab(), for sites that open without switching tabs. */
    void ensureBottomDockOpen();
    /** Shows or hides the active dock tab's side pane. A hidden dock is opened first and the pane is then
     *  shown (never closed); false, and nothing changes, when the active tab has no pane. */
    bool toggleActiveSidePane();

    /** The settings key the user-dragged timeline height round-trips through (the theme metric is only the default). */
    static constexpr const char* kTimelinePanelHeightKey = "timelinePanelHeight";
    /** Whether the WHOLE bottom dock is open, not just the Timeline tab. */
    static constexpr const char* kBottomDockVisibleSettingKey = "bottomDockVisible";
    /** The panel's current docked height in px, always clamped. */
    int getTimelinePanelHeight() const noexcept { return timelinePanelHeight_; }

    synth::TimelineDoc& getTimelineDoc() { return timelineDoc; }
    synth::AutomationRecorder& getAutomationRecorder() { return automationRecorder; }
    void automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    GraphEditor& getGraphEditor() { return graphEditor; }
    ToolbarComponent& getToolbar() { return toolbar; }
    StatusBarComponent& getStatusBar() { return statusBar; }
    synth::AIChatComponent& getAiChatComponent() { return aiChatComponent; }
    ShortcutManager& getShortcutManager() { return shortcutManager; }
    AppUndoManager& getUndoManager() { return undoManager; }
    AudioEngine& getAudioEngine() { return audioEngine; }
    const juce::String& getCurrentPatchName() const { return currentPatchName_; }
    /** Fires whenever the window title text (patch name + dirty marker) should be re-read; MainWindow wires it to
     * setName(). */
    std::function<void(const juce::String&)> onDocumentTitleChanged;

    // ---- Test/automation seams: each prompt callback, when set, REPLACES the real async dialog ----
    std::function<void(const juce::String& actionLabel, std::function<void(UnsavedChangesChoice)> onChoice)>
        unsavedChangesPrompt;
    std::function<void(std::function<void(AutosaveRecoveryChoice)> onChoice)> autosaveRecoveryPrompt;
    std::function<void(std::function<void(PatchLoadMode)> onChoice)> patchLoadPrompt;
    synth::MidiExportSeams midiExportSeams;
    synth::CollectArchiveSeams collectArchiveSeams;

    /** True once an undo-able edit has happened since the last save/load; NOT reset by undoing back to the saved state.
     */
    bool isProjectDirty() const { return isDirty_; }

    /** Asynchronous: `proceed` runs only if the user Saves/Discards, never on Cancel or a failed save. */
    void guardUnsavedChanges(const juce::String& actionLabel, std::function<void()> proceed);

    void openPresetFromFile();
    void openProjectFromFile();
    /** Opens a `.agsproj` bundle named on the command line (or by the OS), guarded like any Open. Message thread only.
     */
    void openProjectFromCommandLine(const juce::File& bundle);

    // ---- Snippets ----
    void refreshSnippetLibrary();
    void promptSaveSnippet();
    void promptRepeatSelection();
    ModuleLibraryComponent& getModuleLibrary() { return moduleLibrary; }

    // ---- Hosted plugins ----
    /** Always go through this, never the member: on the plugin path the processor's longer-lived service is adopted. */
    synth::PluginScanService& getPluginScanService() noexcept { return *activeScanService; }
    const synth::PluginScanService& getPluginScanService() const noexcept { return *activeScanService; }
    void startPluginScan();
    void maybeStartEagerPluginScan();
    /** The status-bar progress reporter both scan triggers post while a scan runs. */
    synth::PluginScanService::ProgressFn makePluginScanProgressReporter();
    /** Writes the scan list into appProperties under kPluginScanListKey. */
    void savePluginScanList();
    /** Pushes the scan list into the library sidebar's Plugins section. */
    void refreshPluginLibrary();
    /** Shared with the plugin processor, which restores the same list. */
    static constexpr const char* kPluginScanListKey = synth::kPluginScanListSettingKey;

    // ---- Recent projects ----
    synth::RecentProjects& getRecentProjects() noexcept { return recentProjects; }
    /** Writes the recent-projects list into appProperties under kRecentProjectsKey. */
    void saveRecentProjects();
    static constexpr const char* kRecentProjectsKey = synth::kRecentProjectsSettingKey;

    void rebuildGraphForLatencyChange();

    // ---- Test-only hooks: see MainComponentTestSeams.cpp for what each one bypasses ----
    synth::midi::MidiLearnController& getMidiLearnControllerForTest() noexcept { return midiLearnController_; }
    synth::midi::RemoteEngine& getRemoteEngineForTest() noexcept { return remoteEngine; }
    bool midiRemoteDevicesOpenedAfterEngineUpForTest() const noexcept { return midiRemoteDevicesOpenedAfterEngineUp_; }
    const auto& getCommandTableForTest() const { return commandTable(); } // CommandSpec stays private; read via auto
    void setUrlOpenerForTest(std::function<void(const juce::URL&)> opener);
    std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption> getMidiDestinationOptionsForTest(synth::TrackId id);
    void setMidiDestinationConnectedForTest(synth::TrackId id, juce::uint32 nodeUid, bool connect);
    void setEditSurfaceOverrideForTest(std::optional<EditSurface> surface);
    /** Stands in for the real keyboard focus, which needs a native peer a headless test has none of. */
    void setFocusedComponentOverrideForTest(juce::Component* focused) { focusedComponentOverrideForTest_ = focused; }
    bool isAiPanelConfiguredVisible() const { return isAiPanelVisible; }
    bool isLibraryConfiguredVisible() const { return isLibraryVisible; }
    bool isBottomDockConfiguredVisible() const { return isBottomDockVisible; }
    void simulateToggleAiPanelClick();
    void simulateToggleModMatrixClick();
    void simulateToggleMinimapClick();
    void simulateToggleLibraryClick();
    void simulateToggleBottomPanelClick();
    void simulateNewPatchClick();
    void simulateUndoClick();
    void simulateRedoClick();
    float getPanelOpenProgressForTest(SlidingPanel p) const noexcept;
    void setPanelOpenProgressForTest(SlidingPanel p, float progress);
    float getPanelSlideStartForTest(SlidingPanel p) const noexcept;
    bool isPanelSlideAnimatingForTest() const noexcept;
    synth::ui::MixerPlacementController& getMixerPlacementControllerForTest() { return mixerPlacement_; }
    synth::MidiRecorder& getMidiRecorderForTest() { return midiRecorder; }
    void simulateAddMidiTrackClick();
    void simulateAddAudioTrackClick();
    void simulateAddInstrumentTrackClick(int menuId);
    bool saveProjectForTest(const juce::File& file);
    void newPatchForTest();
    bool openProjectForTest(const juce::File& file);
    bool openPatchForTest(const juce::File& file, bool append);
    void runAutosaveTickForTest();
    void pollTransportEditsForTest(juce::uint32 nowMs);
    void setAutosaveElapsedMsForTest(juce::uint32 elapsedMs);
    bool isRecordingActiveForTest() const;
    void setAudioTakeCapturingForTest(bool capturing);
    bool wouldPromptOnSaveForTest() const;
    void exportPatchOnlyForTest(const juce::File& file);
    juce::File patchDialogDirectoryForTest() { return patchDialogDirectory(); }
    void relinkClipAssetForTest(synth::ClipId id, const juce::File& chosenFile);
    void importAudioFileToClipForTest(synth::TrackId track, double startBeat, const juce::File& sourceFile);
    int cleanUnusedAssetsForTest();
    synth::ui::WelcomeScreenComponent* getWelcomeScreenForTest() const {
        return welcomeScreen_.get();
    } // null in Hosted mode
    void loadPresetGuardedForTest(int index);
    synth::ui::FocusRegionRegistry& getFocusRegionsForTest() { return focusRegions_; }
    juce::ApplicationProperties& getAppPropertiesForTest() { return appProperties; }
    synth::theme::AppLookAndFeel& getLookAndFeelForTest() { return *lookAndFeel; } // non-null in every ctor
    bool hasTracksNeedingChannelsForTest() const;
    int getStatusBarTickCountForTest() const { return statusBarTickCount_; }
    void simulateLoadFactoryPresetForTest(int index);
    juce::String
    insertTrackPresetFromFileForTest(const juce::File& file); // inserted track's name, or empty on rejection
    synth::AIIntegrationService& getAiServiceForTest() { return aiService; }

private:
    // ---- Command table: backs getAllCommands/getCommandInfo/perform (MainComponentCommandTable.cpp) ----
    struct CommandSpec {
        juce::CommandID id;
        const char* name;
        const char* description;
        const char* category;
        const char* actionId;                               // nullptr = menu-only, no default keypress
        std::function<bool(const MainComponent&)> isActive; // empty = always active
        std::function<bool(MainComponent&)> run;            // returns what perform() returns for this case
    };
    const std::vector<CommandSpec>& commandTable() const;
    static std::vector<CommandSpec> buildGeneralCommandRows();
    static std::vector<CommandSpec> buildEditAndGraphCommandRows();
    static std::vector<CommandSpec> buildTimelineAndPanelCommandRows();
    static std::vector<CommandSpec> buildFocusAndHelpCommandRows();
    static std::vector<CommandSpec> buildTransportCommandRows();
    static std::vector<CommandSpec> buildSelectionStepCommandRows();

    // Named perform() bodies.
    bool performLocateMaster();
    bool performSelectAllModules();
    bool performCopySelection();
    bool performPasteSelection();
    bool performDuplicateSelection();
    bool performCutSelection();
    bool applySnapCommand(juce::CommandID commandID); // all 10 snap commands; id says which
    bool applyZoomCommand(juce::CommandID commandID); // all 4 zoom commands; id says which

    // Named isActive predicates shared by more than one row.
    bool isExportAvailable() const;
    bool isCollectArchiveAvailable() const;
    bool hasSelection() const;
    bool canGroupSelection() const;
    bool touchesAnyMacro() const;
    bool isEditSurfaceCommandActive(juce::CommandID id) const; // Copy/Paste/Duplicate/Cut/Repeat
    bool isBottomDockVisibleForSnap() const;
    bool isZoomCommandActive(juce::CommandID id) const;
    bool isWelcomeScreenHidden() const;

    void pluginScanCompleted(const synth::PluginScanService::Result& result) override;

    void aiPatchAboutToApply() override;
    void aiPatchApplied() override;

    // ---- Timeline app wiring ----
    void timelineChanged(const synth::TimelineDoc& doc) override;
    void publishTimelineAndRebindRecorder();
    void reconcileTimelineAfterGraphChange();
    void reconcileTimelineBindingsOnly();

    void buildInstrumentTrackAndChain(std::unique_ptr<juce::AudioProcessor> instrumentProcessor,
                                      const juce::String& trackNamePrefix, bool poly);

    using InstrumentChainBuild = synth::maincomponent::InstrumentChainBuild;
    bool createTrackInForInstrumentChain(int index, const juce::String& trackNamePrefix, juce::String& trackName,
                                         InstrumentChainBuild& build);
    bool adoptInstrumentNodeForChain(std::shared_ptr<std::unique_ptr<juce::AudioProcessor>> stagedInstrument, int index,
                                     bool poly, InstrumentChainBuild& build);
    void buildInstrumentEnvelopeChain(InstrumentChainBuild& build);
    bool buildInstrumentChannelAndMacro(const juce::String& trackName, InstrumentChainBuild& build);
    /** Shared insert path; opens NO undo transaction of its own -- the caller must. */
    juce::String insertTrackFromPresetVar(const juce::var& preset, synth::TrackPresetKind kind,
                                          const juce::String& trackNamePrefix);
    juce::String insertTrackPresetFromFile(const juce::File& file);
    juce::String insertBusFromPresetVar(const juce::var& preset);
    void handleMacroTrackPresetAction(const juce::String& macroId, bool setAsDefault);
    void saveBusAsPreset(const juce::String& macroId);

    /** Hosted-plugin instrument loads in flight; owns the staged processor until the load completes. */
    std::vector<std::unique_ptr<juce::AudioProcessor>> pendingInstrumentPluginLoads_;
    void dropPendingInstrumentPluginLoad(juce::AudioProcessor* processor);

    void updateRoundTripLatencyReadout();
    void installHostedPluginObservers();
    void commitMidiRecording();

    // ---- Audio recording ----
    using AudioTake = synth::maincomponent::AudioTake;
    juce::AudioProcessorGraph::Node* ensureMasterRecordTap();
    RecordTapModule* findMasterRecordTap() const;
    bool chooseTakeFiles(AudioTake& take) const;
    void commitAudioRecording();

    /** RAII suspension of automation capture for the duration of a programmatic rewrite. */
    struct ProgrammaticApplyScope {
        explicit ProgrammaticApplyScope(MainComponent& owner)
            : guard(owner.automationRecorder) {}
        synth::AutomationRecorder::ScopedProgrammaticApply guard;
    };

    // ---- TrackHeaderHost ----
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId forTrack) override;
    juce::String getNodeDisplayName(const juce::String& uuid) override;
    juce::String getParameterDisplayName(const juce::String& uuid, const juce::String& paramId) override;
    void bindTrackTo(synth::TrackId track, const juce::String& uuid) override;
    void createAndBindTrackInNode(synth::TrackId track) override;
    void selectNodeInGraph(const juce::String& uuid) override;
    void deleteTrack(synth::TrackId track) override;
    void performTrackEdit(const std::function<void()>& mutation) override;
    void addMidiTrack() override;
    void addAudioTrack() override;
    void addInstrumentTrack(const juce::String& instrumentModuleType, bool poly) override;
    bool hasTracksNeedingChannels() const override;
    void createChannelsForExistingTracks() override;
    bool canMakeChannelForTrack(synth::TrackId track) const override;
    void makeChannelForTrack(synth::TrackId track) override;
    bool canSaveTrackPresetForTrack(synth::TrackId track) const override;
    void saveTrackAsPreset(synth::TrackId track) override;
    void setTrackPresetAsDefault(synth::TrackId track) override;
    void addTrackFromPreset(const juce::String& presetName, synth::TrackPresetKind kind) override;
    void addTrackFromPresetFile() override;
    void addBusFromPreset(const juce::String& presetName) override;
    void makeChannelForNode(juce::AudioProcessorGraph::NodeID source);
    void duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);
    std::vector<synth::PluginIdentity> getInstrumentPluginOptions() const override;
    bool isPluginScanInProgress() const override;
    void ensureInstrumentPluginsScanned() override;
    void addInstrumentPluginTrack(const synth::PluginIdentity& identity) override;
    std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> getAvailablePluginLaneOptions() const override;
    synth::LaneId addPluginAutomationLane(const synth::ui::TrackHeaderHost::PluginLaneOption& option) override;
    juce::ApplicationProperties* getAppProperties() override;
    std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption>
    getMidiDestinationOptions(synth::TrackId forTrack) override;
    void setMidiDestinationConnected(synth::TrackId forTrack, juce::uint32 nodeUid, bool connect) override;
    void auditionTrackNote(synth::TrackId forTrack, int pitch, int velocity, bool noteOn) override;
    synth::ui::TrackChannelLinkSurface* getChannelLinkSurface() override;

    juce::String createTrackInNode();
    juce::String createTrackAudioNode(bool wireDirectlyToMasterBus = true);
    void refreshAssetRoots();

    // ---- Asset management (import/relink/collect-clean/adopt-on-save) ----
    void promptRelinkClipAsset(synth::ClipId id);
    void relinkClipAsset(synth::ClipId id, const juce::File& chosenFile);
    void importAudioFileToClip(synth::TrackId track, double startBeat, const juce::File& sourceFile);
    double audioFileLengthInBeats(const juce::File& file) const;
    int cleanUnusedAssets();
    juce::AudioProcessorGraph::Node* findNodeByUuid(const juce::String& uuid) const;

    // ---- File handlers, minus the dialogs ----
    bool saveToFile(const juce::File& file);
    bool openFromFile(const juce::File& file, bool append = false);
    bool loadBundleFromFile(const juce::File& bundleDir);
    bool loadAutosaveFromFile(const juce::File& bundleDir);
    void applyAutosaveRecoveryAnswer(AutosaveRecoveryChoice choice, const juce::File& bundleDir);
    void promptAutosaveRecovery(std::function<void(AutosaveRecoveryChoice)> onChoice);
    void promptPatchLoadMode(std::function<void(PatchLoadMode)> onChoice);
    void performSaveProject(bool forceChooser, std::function<void(bool saved)> onFinished = {});
    void exportPatchOnly(const juce::File& file);
    void promptExportPatchOnly();
    // Start folder of the patch dialogs (Open Patch / Export Patch Only), per the patch-save-location preference.
    juce::File patchDialogDirectory();
    void promptExportAudio();
    void promptExportStems();
    void promptExportMidi();
    // ---- Collect & Archive (MainComponentCollectArchive.cpp); message thread only ----
    void promptCollectAndArchive();
    void startCollect(bool thenZip); // requires currentBundleDir_ to be a saved bundle
    void finishCollect(const synth::CollectResult& result, bool cancelled, bool thenZip);
    void promptArchiveFile();
    void writeProjectArchive(const juce::File& zipFile);
    void runProjectTask(const juce::String& title, bool cancellable, synth::CollectArchiveSeams::Work work,
                        std::function<void(bool)> done);
    void reportCollectArchive(const juce::String& title, const juce::String& message);
    void loadFactoryPresetAtIndex(int index);
    void loadPresetGuarded(int index, bool isNewDocument = false);
    void openRecentProjectGuarded(const juce::File& file);
    void clearTimelineForNewPatch();
    void newPatch();
    void launchOpenPresetChooser();
    void launchOpenProjectChooser();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void globalFocusChanged(juce::Component* focusedComponent) override;

    /** The Settings dialog; `initialiseTabName` empty = the last-used tab. */
    void launchSettingsWindow(const juce::String& initialTabName);

    void initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);

    // ---- initialiseCommon()'s ordered setup steps: declared in call order, and that order IS the contract ----
    void migrateBottomDockVisibleSettingKey();
    void restorePanelPreferences();
    void restoreGraphEditorPreferences();
    void configureAiProvider(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);
    void wireAiChatAndAccount();
    void wireGraphEditorCallbacks();
    void wirePluginScanAndRecents();
    void wireCommandsAndShortcuts();
    void wireMidiRemoteEngine();
    void addCanvasAndPanels();
    void addToolbarChrome();
    void addFileButtons();
    void showLoadMenu();
    void addUndoButtons();
    void wireTimelinePanel();
    void wireTimelinePanelServicesAndShortcuts();
    void wireTimelineHookInventory();
    void wireTimelineClipLaneCallbacks();
    void wireTimelineRecordToggle();
    void handleRecordToggle(bool wantRecording);
    void addToolbarToggleButtons();
    void assembleToolbar();
    void wireStatusBar();
    bool initialiseAudioEngine();
    void openMidiRemoteDevices(); // standalone-only continuation of wireMidiRemoteEngine()
    void createWelcomeScreen();
    void registerFocusRegions();
    /** The clear+rebuild half of registerFocusRegions(); call only from there or after a detach/redock. */
    void rebuildFocusRegions();

    void applyToolbarIcons();
    // Sets every toolbar button's screen-reader name, and its visible text unless `iconOnly`.
    void applyToolbarLabels(bool iconOnly);
    void applyStoredDualIOPreferenceToPatch();
    juce::String computeOutputDeviceInfoText() const;
    void setLibraryVisible(bool v);

    // ---- Welcome screen ----
    void hideWelcomeScreen();
    void showWelcomeScreen();
    void showWhatsNewDialog();
    /** Opens the contribute URL through urlOpener_ (default: the system browser). */
    void openContributePage();

    // ---- Timeline panel height (user-resizable, persisted) ----
    int defaultTimelinePanelHeight() const;
    int clampTimelinePanelHeight(int desiredHeight) const;
    void setTimelinePanelHeight(int desiredHeight, bool persist);

    void setCurrentPatchName(const juce::String& name);
    void markDocumentClean();
    bool isRecordingActive() const;
    void maybeAutosave();
    void performAutosave();
    void pollTransportEdits(juce::uint32 nowMs);
    void applyLoadedTransport(const synth::TransportDoc& loaded);
    void notifyDocumentTitleChanged();

    void promptUnsavedChanges(const juce::String& actionLabel, std::function<void(UnsavedChangesChoice)> onChoice);
    void applyUnsavedChangesAnswer(UnsavedChangesChoice choice, std::function<void()> proceed);

    // ---- Members. DECLARATION ORDER IS LOAD-BEARING (construction, destruction and reference
    // validity all follow it); the reasons are written up in MainComponent.cpp above the ctors. ----
    std::unique_ptr<synth::theme::ThemeManager> ownedThemeManager; // null unless the delegating ctor ran
    std::unique_ptr<synth::theme::AppLookAndFeel> ownedLookAndFeel;
    synth::theme::ThemeManager* themeManager{nullptr}; // always valid: external or the owned fallback
    synth::theme::AppLookAndFeel* lookAndFeel{nullptr};

    synth::TimelineDoc timelineDoc;               // the app's ONE live timeline; precedes undoManager
    synth::AutomationRecorder automationRecorder; // precedes undoManager
    synth::MidiRemoteProjectDoc midiRemoteDoc;    // precedes undoManager
    synth::MidiRecorder midiRecorder;

    AppUndoManager undoManager;

    std::unique_ptr<AudioEngine> ownedAudioEngine; // standalone paths only
    AudioEngine& audioEngine;                      // the single access point either way

    synth::PluginCardLayoutStore pluginCardLayoutStore; // declared before graphEditor: outlives its listeners
    std::unique_ptr<GraphEditor> graphEditorOwner_;     // heap-held so this header need not include GraphEditor.h
    GraphEditor& graphEditor;

    std::unique_ptr<synth::ui::WelcomeScreenComponent>
        welcomeScreen_; // null in Hosted mode; added last so it paints on top

    synth::HostedPluginWindowManager pluginWindowManager; // declared after the engine + graphEditor so it dies first

    ModuleLibraryComponent moduleLibrary;

    ToolbarComponent toolbar; // the strip; the buttons below stay direct children of MainComponent

    juce::DrawableButton newButton{"new", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton saveButton{"save", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton loadButton{"load", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton settingsButton{"settings", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton feedbackButton{"feedback", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton undoButton{"undo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton redoButton{"redo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleAiPanelButton{"toggleAi", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleModMatrixButton{"toggleMatrix", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleMinimapButton{"toggleMinimap", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton autoArrangeButton{"autoArrange", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleLibraryButton{"toggleLibrary", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleBottomPanelButton{"toggleBottomPanel", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton themeToggleButton{"toggleTheme", juce::DrawableButton::ImageAboveTextLabel};

    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ApplicationProperties appProperties; // declared before aiChatComponent, whose ctor reads it
    juce::PropertiesFile::Options propertiesOptions;

    synth::AIIntegrationService aiService;
    synth::AccountService accountService{synth::branding::resolveApiBaseUrl()}; // before aiChatComponent
    synth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;
    bool isLibraryVisible{true};
    bool isAlignmentGuidesEnabled{true};

    synth::ui::TrackChannelLinkController trackChannelLink_{audioEngine, timelineDoc, undoManager, graphEditor};

    synth::ui::TimelinePanelComponent timelinePanel;
    synth::ui::BottomDockComponent bottomDock{timelinePanel, audioEngine,   timelineDoc, undoManager,
                                              graphEditor,   appProperties, lookAndFeel, &shortcutManager};
    synth::ui::MixerPlacementController mixerPlacement_{bottomDock, appProperties}; // after bottomDock
    bool isBottomDockVisible = false;
    bool bottomDockAutoHiddenByEmptyTabs_ = false; // dock auto-hid because its last tab detached
    bool fitMixerAfterDockSlide_ = false;          // set when the dock starts opening; see finishPanelSlide()
    int timelinePanelHeight_ = 0;                  // 0 only before initialiseCommon() resolves it
    bool wasTransportPlaying_ = false;             // playing->stopped edge for the MIDI recorder's auto-commit
    synth::TransportNudgeState transportNudge_;    // message-thread memory of the last cursor-move request

    bool feedbackGuardLatched_ = false; // re-arm latch; cleared on the none-armed -> armed edge
    bool wasAnyAudioTrackArmed_ = false;

    AudioTake audioTake_;
    juce::File currentBundleDir_; // invalid File = never saved; decides where a take is written

    std::vector<std::unique_ptr<ProgrammaticApplyScope>> programmaticApplyScopes; // stack: undo/redo restore span
    std::unique_ptr<ProgrammaticApplyScope>
        aiApplyScope; // AI apply span; its own slot because the pair may be unbalanced

    bool toolbarNarrowMode_{false}; // applyToolbarIcons() re-clones icons only on the transition
    int statusBarTickCount_{0};     // status bar updates every 2nd 10 Hz tick

    juce::String currentPatchName_{"Default"}; // declared before statusBar
    StatusBarComponent statusBar;
    bool isDirty_ = false;    // recomputed from savedEditSerial_; never write false directly (use markDocumentClean())
    int savedEditSerial_ = 0; // AppUndoManager edit serial at the last save/load/new document
    int documentGeneration_ = 0; // bumped by guardUnsavedChanges() just before `proceed`; stale async loads compare it
    int lastAutosavedEditSerial_ = 0; // autosave's own baseline, separate from savedEditSerial_
    juce::uint32 lastAutosaveMs_ = 0; // wall-clock (getMillisecondCounter) of the last autosave write

    synth::TransportDoc committedTransport_; // the transport state the undo history and document last recorded
    synth::TransportDoc pendingTransport_;   // a changed value waiting out its debounce
    juce::uint32 pendingTransportSinceMs_ = 0;

    bool isBounceInProgress_ = false;  // ONE flag for Export Audio and Export Stems: the offline render is exclusive
    bool isCollectInProgress_ = false; // a Collect copy or archive zip is running behind its progress window
    std::unique_ptr<synth::BounceRunner> bounceRunner_;
    std::unique_ptr<synth::StemRunner> stemRunner_; // at most one of bounceRunner_/stemRunner_ is non-null
    juce::Component::SafePointer<synth::ui::ExportAudioDialog> exportDialog_; // owned by its DialogWindow, never by us

    synth::PluginScanService pluginScanService;
    synth::PluginScanService* activeScanService = &pluginScanService; // ours or the adopted one; never null

    synth::RecentProjects recentProjects;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    synth::midi::MidiRemoteFeedbackOutputs
        remoteFeedbackOutputs_; // declared BEFORE remoteEngine, which holds a raw sink pointer
    synth::midi::RemoteEngine remoteEngine;
    MainComponentRemoteActionInvoker remoteActionInvoker_{commandManager, audioEngine, undoManager, transportNudge_,
                                                          timelineDoc};
    synth::midi::MidiLearnController midiLearnController_{audioEngine,   graphEditor, remoteEngine,
                                                          midiRemoteDoc, undoManager, statusBar};
    juce::Component::SafePointer<juce::Component> focusedComponentOverrideForTest_;
    std::optional<EditSurface> editSurfaceOverrideForTest_; // consulted first by resolveEditSurface()
    bool midiRemoteDevicesOpenedAfterEngineUp_ = false;     // set by openMidiRemoteDevices(); test-only read

    synth::ui::FocusRegionRegistry focusRegions_; // a plain member, not a Desktop-global singleton

#if JUCE_MAC || JUCE_WINDOWS
    synth::update::UpdateManager updateManager;
#endif

    std::function<void(const juce::URL&)> urlOpener_ = [](const juce::URL& u) { u.launchInDefaultBrowser(); };

    // ---- Panel slide animations: each panel owns a [0..1] open fraction that resized() derives its size from ----
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver panelSlideAnim_; // ONE driver moves all three panels
    synth::ui::PanelSlide librarySlide_;
    synth::ui::PanelSlide aiPanelSlide_;
    synth::ui::PanelSlide timelineSlide_;

    static constexpr double kPanelSlideMs = 190.0; // shared by all three slides

    const synth::ui::PanelSlide& panelSlide(SlidingPanel p) const noexcept;
    synth::ui::PanelSlide& panelSlide(SlidingPanel p) noexcept;

    /** THE panel-toggle seam: callers flip the flag, persist it, refresh the toolbar, then call this.
     *  Lands synchronously when nothing can animate (headless). */
    void beginPanelSlide();
    // Grows the dock or Own panel so the mixer's sections fit; call only when the mixer is newly shown.
    void fitMixerHostToSections();
    void applyPanelSlideFrame(float t);
    void finishPanelSlide();

    void setAlignmentGuidesEnabled(bool enabled);

    // Constructed last so all child components exist. Do NOT set tooltips here.
    std::unique_ptr<juce::Component> shortcutHints_; // Cmd-hold shortcut hints overlay
    juce::TooltipWindow tooltipWindow{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
