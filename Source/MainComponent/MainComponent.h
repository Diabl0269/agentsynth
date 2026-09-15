#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AccountService.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "Mixer/TrackPresetManager.h"
#include "Modules/RecordTapModule.h"
#include "Plugin/Hosting/HostedPluginWindowManager.h"
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
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Chrome/ExportAudioDialog.h"
#include "UI/Chrome/StatusBarComponent.h"
#include "UI/Chrome/ToolbarComponent.h"
#include "UI/Chrome/WelcomeScreenComponent.h"
#include "UI/Graph/GraphEditor/GraphEditor.h"
#include "UI/Layout/FocusRegion.h"
#include "UI/Layout/UIAnimation.h"
#include "UI/Library/ModuleLibraryComponent/ModuleLibraryComponent.h"
#include "UI/Mixer/MixerDockComponent.h"
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

class MainComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::Timer
    , public juce::ApplicationCommandTarget
    , private juce::ChangeListener
    , private synth::AIIntegrationService::Listener
    // The app owns the one live TimelineDoc, so it is also the thing that republishes it to
    // the audio thread on every edit (timelineChanged) and the thing the track headers ask to
    // create/re-bind/delete their Track In nodes (TrackHeaderHost).
    , private synth::TimelineDoc::Listener
    , private synth::ui::TrackHeaderHost
    // T159: repaints a focus-region root's accent outline when keyboard focus moves into or out of
    // it. Component::focusGained/focusLost are no-op virtuals for most components, so this listener
    // is the one thing that actually triggers the repaint (see FocusRegion.h's paint helper).
    , private juce::FocusChangeListener
    // FRO44: registers on the ONE shared PluginScanService (ours, or the plugin path's adopted one)
    // so the sidebar refreshes and the list is persisted no matter which caller — the sidebar's own
    // "Scan for plugins..." row, or the eager startup scan below — actually triggered the scan.
    , private synth::PluginScanService::Listener {
public:
    // Primary ctor: receives injected ThemeManager and LookAndFeel from Main.cpp.
    // provider is optional (nullptr → reads saved provider pref from appProperties).
    // Owns its own (standalone) AudioEngine, which it initialises and shuts down.
    MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                  std::unique_ptr<synth::AIProvider> provider = nullptr);

    // Plugin ctor: the editor's AudioEngine is owned by AgentSynthAudioProcessor and outlives
    // every editor instance, so it is injected rather than owned here. The engine's lifecycle
    // (initialise/shutdown) belongs to the processor — this component must not touch it, or
    // closing the plugin window would tear down the running graph.
    MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf, AudioEngine& externalEngine,
                  std::unique_ptr<synth::AIProvider> provider = nullptr);

    // Delegating ctor for tests and legacy call sites that don't inject theme objects.
    // Lazily owns private default ThemeManager + AppLookAndFeel instances
    // (stored in ownedThemeManager / ownedLookAndFeel below).
    explicit MainComponent(std::unique_ptr<synth::AIProvider> provider = nullptr,
                           synth::AIProviderRegistry registry = synth::AIProviderRegistry::createDefault());

    ~MainComponent() override;

    void timerCallback() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    // ApplicationCommandTarget
    ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;
    // Test-only: CommandSpec itself stays private -- read via auto (MainComponentCommandTableTests.cpp).
    const auto& getCommandTableForTest() const { return commandTable(); }

    bool keyPressed(const juce::KeyPress& key) override;

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    void updateCommandShortcuts();

    enum class EditSurface { Graph, TimelineClips, PianoRoll };
    EditSurface resolveEditSurface() const;

    // Test-only edit-surface override; consulted before any real focus check. See docs/testing.md.
    void setEditSurfaceOverrideForTest(std::optional<EditSurface> surface) { editSurfaceOverrideForTest_ = surface; }

    bool performRepeatSelection(int count);

    static constexpr int kMinRepeatCount = 1; // Repeat's count bounds (dialog clamp + performRepeatSelection).
    static constexpr int kMaxRepeatCount = 64;

    // P4-6: default AI provider id when none is persisted yet ("remote" for a brand-new install,
    // else the existing "ollama" default). See initialiseCommon() for the caller.
    static juce::String resolveDefaultProviderId(bool hasExistingSettingsFile) {
        return hasExistingSettingsFile ? juce::String("ollama") : juce::String("remote");
    }

    // Testing Hooks
    bool isAiPanelConfiguredVisible() const { return isAiPanelVisible; }
    bool isLibraryConfiguredVisible() const { return isLibraryVisible; }
    void simulateToggleAiPanelClick() {
        if (toggleAiPanelButton.onClick)
            toggleAiPanelButton.onClick();
    }
    void simulateToggleModMatrixClick() {
        if (toggleModMatrixButton.onClick)
            toggleModMatrixButton.onClick();
    }
    void simulateToggleMinimapClick() {
        if (toggleMinimapButton.onClick)
            toggleMinimapButton.onClick();
    }
    void simulateToggleLibraryClick() {
        if (toggleLibraryButton.onClick)
            toggleLibraryButton.onClick();
    }
    void simulateToggleTimelineClick() {
        if (toggleTimelineButton.onClick)
            toggleTimelineButton.onClick();
    }

    /** The three sliding panels this component docks, for the slide test seams below. */
    enum class SlidingPanel { Library, AiChat, Timeline };

    // Panel-slide test seams (docs/layout.md §11); the fractions ARE the layout.
    float getPanelOpenProgressForTest(SlidingPanel p) const noexcept { return panelSlide(p).getProgress(); }
    void setPanelOpenProgressForTest(SlidingPanel p, float progress) {
        panelSlide(p).snapTo(progress);
        resized();
    }
    /** The fraction the in-flight tween STARTED from (never 0 or 1 mid-slide). */
    float getPanelSlideStartForTest(SlidingPanel p) const noexcept { return panelSlide(p).getTweenStart(); }
    /** True only while the shared slide driver is actually running. */
    bool isPanelSlideAnimatingForTest() const noexcept { return panelSlideAnim_.isRunning(); }
    /** The Preferences "Natural scrolling" key. DEFAULT TRUE. See applyNaturalScrollingPreference. */
    static constexpr const char* kNaturalScrollingKey = "naturalScrolling";

    /** Re-reads kNaturalScrollingKey and pushes `!natural` into the timeline panel + piano roll.
     *  Called at startup and on every settings-file change; idempotent. */
    void applyNaturalScrollingPreference();

    /** The Preferences "Scroll up to zoom in" checkbox's key. DEFAULT TRUE. See
     *  applyZoomScrollPreference. */
    static constexpr const char* kZoomScrollUpZoomsInKey = "zoomScrollUpZoomsIn";

    /** Re-reads kZoomScrollUpZoomsInKey and pushes `!upZoomsIn` into the timeline panel (which
     *  forwards it to the piano roll) — independent of applyNaturalScrollingPreference; same
     *  propagation path and idempotence. */
    void applyZoomScrollPreference();

    /** Per-press zoom step for the four zoom commands; the out factor is the exact reciprocal. */
    static constexpr double kZoomInFactor = 1.25;
    static constexpr double kZoomOutFactor = 1.0 / kZoomInFactor;
    bool isTimelineConfiguredVisible() const { return isTimelineVisible; }
    synth::ui::TimelinePanelComponent& getTimelinePanel() { return timelinePanel; }
    synth::ui::MixerDockComponent& getMixerDock() { return mixerDock; }
    /** Opens the dock on the Mixer tab (switching tabs, or opening the dock, as needed); closes it
     *  when already open on the Mixer tab. Mirrors toggleTimelineButton's own open/close symmetry
     *  -- see MainComponentPanels.cpp. */
    void performToggleMixerPanel();

    /** The settings key the user-dragged timeline height round-trips through; the theme metric is
     *  only the DEFAULT — see clampTimelinePanelHeight(). */
    static constexpr const char* kTimelinePanelHeightKey = "timelinePanelHeight";

    /** The panel's current docked height in px, always clamped (see clampTimelinePanelHeight()). */
    int getTimelinePanelHeight() const noexcept { return timelinePanelHeight_; }
    // Test hooks. The doc and the recorder are real app state (not test-only objects), so
    // these are plain accessors; the simulate*/…ForTest entry points below drive the same code
    // paths the buttons and file dialogs do, minus the dialogs.
    synth::TimelineDoc& getTimelineDoc() { return timelineDoc; }
    synth::AutomationRecorder& getAutomationRecorder() { return automationRecorder; }
    void automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    // The app's one live MidiRecorder — see docs/architecture.md. Test-only, mirrors
    // getAutomationRecorder() above.
    synth::MidiRecorder& getMidiRecorderForTest() { return midiRecorder; }
    // juce::PopupMenu never runs in a test process — these drive the "+ Track" menu's own headless
    // seam (TimelinePanelComponent::applyAddTrackMenuChoice) directly.
    void simulateAddMidiTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    }
    void simulateAddAudioTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }
    /** T183 (P9-3b): drives the Instrument submenu's headless seam directly, by menu id. */
    void simulateAddInstrumentTrackClick(int menuId) { timelinePanel.applyAddTrackMenuChoice(menuId); }
    /** Exactly what the Save dialog's callback runs: `.agsproj` writes a bundle, else a preset. */
    bool saveProjectForTest(const juce::File& file) { return saveToFile(file); }
    /** The post-guard half of New Patch only — bypasses guardUnsavedChanges (same idiom as
     *  saveProjectForTest bypassing the save chooser). */
    void newPatchForTest() { newPatch(); }
    /** Exactly what the Open dialog's callback runs: an `.agsproj` bundle directory loads graph +
     *  timeline, anything else a plain `.json` preset. A bundle carrying a pending autosave sidecar
     *  kicks off the async recovery prompt instead and returns true before any load has happened -
     *  a test drives autosaveRecoveryPrompt directly, the same idiom unsavedChangesPrompt uses. */
    bool openProjectForTest(const juce::File& file) { return openFromFile(file); }
    // P8-31: reaches openFromFile's PATCH branch with an explicit load mode (`append == true` adds
    // onto the live graph; false replaces it), without a native file chooser.
    bool openPatchForTest(const juce::File& file, bool append) { return openFromFile(file, append); }
    /** Runs performAutosave()'s exact gate check once, synchronously — the same call
     *  timerCallback() makes on every tick, exposed so a test can drive it without a real
     *  juce::Timer. */
    void runAutosaveTickForTest() { maybeAutosave(); }
    /** Test-only: back-dates the "last autosave" wall-clock baseline by `elapsedMs`, so a test can
     *  simulate the configured interval having elapsed without a real sleep. Computed relative to
     *  the CURRENT counter (rather than writing a fixed small value) so it is correct regardless of
     *  how large juce::Time::getMillisecondCounter() already is when the test runs. */
    void setAutosaveElapsedMsForTest(juce::uint32 elapsedMs) {
        lastAutosaveMs_ = juce::Time::getMillisecondCounter() - elapsedMs;
    }
    /** True once an audio or MIDI take is capturing — see isRecordingActive(). Test-only seam so a
     *  test can assert the autosave gate is actually reading live recording state without making
     *  AudioTake/MidiRecorder internals public. */
    bool isRecordingActiveForTest() const { return isRecordingActive(); }
    /** Test-only: forces AudioTake::capturing without the real record-arm/record-button/master-tap
     *  machinery — used only to verify the autosave gate respects this flag. Never commits a clip;
     *  callers must reset it back to false before the test ends. */
    void setAudioTakeCapturingForTest(bool capturing) { audioTake_.capturing = capturing; }
    /** What performSaveProject(false) will do next: true if there's no bundle to resave to
     *  silently, so Cmd+S is about to prompt for a location. */
    bool wouldPromptOnSaveForTest() const {
        return !(currentBundleDir_ != juce::File() && synth::ProjectBundle::isBundle(currentBundleDir_));
    }
    /** Exactly what the "Export Patch Only" menu item's chooser callback runs once the user has
     *  picked a file — bypasses the async dialog itself, same idiom as saveProjectForTest. */
    void exportPatchOnlyForTest(const juce::File& file) { exportPatchOnly(file); }
    /** Exactly what the production "Relink audio…" FileChooser callback runs once the user
     *  has picked a file — bypasses the async dialog itself, same idiom as saveProjectForTest. */
    void relinkClipAssetForTest(synth::ClipId id, const juce::File& chosenFile) { relinkClipAsset(id, chosenFile); }
    /** Exactly what the clip lane area reports when the user drops an audio file on an audio row
     *  (or picks one from the double-click chooser) — bypasses the OS drag/dialog, same idiom as
     *  relinkClipAssetForTest. */
    void importAudioFileToClipForTest(synth::TrackId track, double startBeat, const juce::File& sourceFile) {
        importAudioFileToClip(track, startBeat, sourceFile);
    }
    /** Sweeps `<bundle>/Audio/` (+ `Peaks/`) for files no clip in the live timeline
     *  references and deletes exactly those — see synth::AssetManager::cleanUnusedAssets. A no-op
     *  (returns 0) outside a saved bundle. Not wired to any menu/shortcut yet — see
     *  docs/architecture.md's asset-management subsection for why. */
    int cleanUnusedAssetsForTest() { return cleanUnusedAssets(); }
    GraphEditor& getGraphEditor() { return graphEditor; }
    // T114/P8-10: null in Hosted mode (the plugin path never constructs one — see
    // ownedAudioEngine's gate in initialiseCommon()).
    synth::ui::WelcomeScreenComponent* getWelcomeScreenForTest() const { return welcomeScreen_.get(); }
    ToolbarComponent& getToolbar() { return toolbar; }
    StatusBarComponent& getStatusBar() { return statusBar; }
    // The docked AI chat panel — plain accessor (the panel-slide tests read its bounds mid-slide).
    synth::AIChatComponent& getAiChatComponent() { return aiChatComponent; }
    ShortcutManager& getShortcutManager() { return shortcutManager; }
    synth::ui::FocusRegionRegistry& getFocusRegionsForTest() { return focusRegions_; }
    void simulateNewPatchClick() {
        if (newButton.onClick)
            newButton.onClick();
    }
    void simulateUndoClick() {
        if (undoButton.onClick)
            undoButton.onClick();
    }
    void simulateRedoClick() {
        if (redoButton.onClick)
            redoButton.onClick();
    }
    AppUndoManager& getUndoManager() { return undoManager; }
    AudioEngine& getAudioEngine() { return audioEngine; }
    const juce::String& getCurrentPatchName() const { return currentPatchName_; }
    /** Fires whenever the window title text (patch name + dirty marker) should be re-read — see
     *  notifyDocumentTitleChanged(). Main.cpp's MainWindow wires this to its own setName(). */
    std::function<void(const juce::String&)> onDocumentTitleChanged;

    /** What the user picked in the unsaved-changes dialog: Save runs performSaveProject; Discard
     *  continues immediately; Cancel abandons the action that asked. */
    enum class UnsavedChangesChoice { Save, Discard, Cancel };

    /** Test/automation seam: when set, REPLACES the real async juce::AlertWindow. */
    std::function<void(const juce::String& actionLabel, std::function<void(UnsavedChangesChoice)> onChoice)>
        unsavedChangesPrompt;

    /** What the user picked when openFromFile found a pending autosave sidecar on the bundle being
     *  opened. Restore loads autosave.json instead of project.json and leaves the document dirty
     *  (the loaded state diverges from what's on disk); Discard loads project.json normally. Either
     *  arm deletes the sidecar afterwards — see ProjectBundle::discardAutosave. */
    enum class AutosaveRecoveryChoice { Restore, Discard };

    /** Test/automation seam for the autosave-recovery prompt, same idiom as unsavedChangesPrompt:
     *  when set it REPLACES the real async juce::AlertWindow. */
    std::function<void(std::function<void(AutosaveRecoveryChoice)> onChoice)> autosaveRecoveryPrompt;

    /** The choice the user makes when opening a `.json` patch (P8-31): replace the current patch,
     *  add the loaded one on top of it, or cancel the open. */
    enum class PatchLoadMode { Replace, Append, Cancel };

    /** Test/automation seam for the patch load-mode prompt, same idiom as unsavedChangesPrompt and
     *  autosaveRecoveryPrompt: when set it REPLACES the real async juce::AlertWindow. The load only
     *  happens when the seam fires a non-Cancel choice. */
    std::function<void(std::function<void(PatchLoadMode)> onChoice)> patchLoadPrompt;

    /** True once an undo-able edit has happened since the last save/load - see changeListenerCallback's
     *  AppUndoManager branch. Deliberately NOT reset by undoing back to the state that was saved -
     *  see the dirty-state section of docs/architecture.md for why a false "clean" is the dangerous
     *  direction. */
    bool isProjectDirty() const { return isDirty_; }

    void guardUnsavedChanges(const juce::String& actionLabel, std::function<void()> proceed);
    // Non-const access to ApplicationProperties for persistence tests (read-back within session).
    juce::ApplicationProperties& getAppPropertiesForTest() { return appProperties; }
    // FRO26 (P9-3e, docs/mixer.md §5.13): hasTracksNeedingChannels() is a private TrackHeaderHost
    // override (MainComponent inherits that interface privately), so a test can't call it directly
    // the way it can drive the menu action itself via getTimelinePanel().applyAddTrackMenuChoice() —
    // this thin public wrapper is the same idiom as newPatchForTest() above, just for a query
    // instead of a mutation, so a test can assert the "+ Track" menu's own enabled/disabled state.
    bool hasTracksNeedingChannelsForTest() const { return hasTracksNeedingChannels(); }
    int getStatusBarTickCountForTest() const { return statusBarTickCount_; }
    void simulateLoadFactoryPresetForTest(int index);
    void openPresetFromFile();
    void openProjectFromFile();
    synth::AIIntegrationService& getAiServiceForTest() { return aiService; }

    // ---- Snippets (issue #156) ----

    void refreshSnippetLibrary();

    void promptSaveSnippet();

    void promptRepeatSelection();

    ModuleLibraryComponent& getModuleLibrary() { return moduleLibrary; }

    // ---- Hosted plugins ----
    //
    // MainComponent owns a scan list (Core must not touch settings; a scan is refused outright on a
    // Hosted engine, so a DAW session can never trigger a nested scan) but it is not always the list
    // in use: on the plugin path AgentSynthAudioProcessor installs its OWN, longer-lived service
    // before any editor exists, and an editor on an external engine ADOPTS that rather than replacing
    // it. Everything below goes through getPluginScanService(), never the member directly.

    synth::PluginScanService& getPluginScanService() noexcept { return *activeScanService; }
    const synth::PluginScanService& getPluginScanService() const noexcept { return *activeScanService; }

    void startPluginScan();

    void maybeStartEagerPluginScan();

    /** Writes the scan list into appProperties under "pluginScanList". */
    void savePluginScanList();

    /** Pushes the scan list into the library sidebar's Plugins section. */
    void refreshPluginLibrary();

    /** The settings key the scan list round-trips through — shared with the plugin processor, which
     *  restores the same list. */
    static constexpr const char* kPluginScanListKey = synth::kPluginScanListSettingKey;

    // ---- Recent projects ----

    /** The recent-projects list the Load menu's "Recent Projects" section is built from. Single
     *  owner (unlike the scan list above) — see kRecentProjectsSettingKey's comment. */
    synth::RecentProjects& getRecentProjects() noexcept { return recentProjects; }

    /** Writes the recent-projects list into appProperties under "recentProjects". */
    void saveRecentProjects();

    /** The settings key the recent-projects list round-trips through. */
    static constexpr const char* kRecentProjectsKey = synth::kRecentProjectsSettingKey;

    void rebuildGraphForLatencyChange();

private:
    // ---- Command table (FRO76) -- backs getAllCommands/getCommandInfo/perform, table order is
    // getAllCommands() order; name == nullptr derives it via ShortcutManager::getActionDescription
    // (the snap/zoom blocks). See MainComponentCommandTable.cpp.
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
    // FRO11: commandTable()'s own row groups, split out to keep it under the function-size cap.
    static std::vector<CommandSpec> buildGeneralCommandRows();
    static std::vector<CommandSpec> buildEditAndGraphCommandRows();
    static std::vector<CommandSpec> buildTimelineAndPanelCommandRows();
    static std::vector<CommandSpec> buildFocusAndHelpCommandRows();

    // Named perform() bodies, too long for an inline table lambda.
    bool performLocateMaster();
    bool performSelectAllModules();
    bool performCopySelection();
    bool performPasteSelection();
    bool performDuplicateSelection();
    bool performCutSelection();
    bool applySnapCommand(juce::CommandID commandID); // all 10 snap commands; id says which
    bool applyZoomCommand(juce::CommandID commandID); // all 4 zoom commands; id says which

    // Named isActive predicates shared by more than one row.
    bool isExportAvailable() const { return !isBounceInProgress_; }
    bool hasSelection() const { return graphEditor.getSelectionCount() > 0; }
    bool canGroupSelection() const { return graphEditor.getSelectionCount() > 1 || touchesAnyMacro(); }
    bool touchesAnyMacro() const;
    bool isEditSurfaceCommandActive(juce::CommandID id) const; // Copy/Paste/Duplicate/Cut/Repeat
    bool isTimelineVisibleForSnap() const { return isTimelineVisible; }
    bool isZoomCommandActive(juce::CommandID id) const;
    bool isWelcomeScreenHidden() const { return welcomeScreen_ == nullptr || !welcomeScreen_->isVisible(); }

    void pluginScanCompleted(const synth::PluginScanService::Result& result) override;

    void aiPatchAboutToApply() override;
    void aiPatchApplied() override;

    // ---- Timeline app wiring. ----

    void timelineChanged(const synth::TimelineDoc& doc) override;

    void publishTimelineAndRebindRecorder();

    void reconcileTimelineAfterGraphChange();

    void reconcileTimelineBindingsOnly();

    void buildInstrumentTrackAndChain(std::unique_ptr<juce::AudioProcessor> instrumentProcessor,
                                      const juce::String& trackNamePrefix, bool poly);

    // ---- buildInstrumentTrackAndChain step state (FRO76 §C) -- see MainComponentTrackCreation.cpp ----
    struct InstrumentChainBuild {
        synth::TrackId trackId;
        juce::AudioProcessorGraph::Node* trackInNode = nullptr;
        juce::String trackInUuid;
        juce::Point<int> trackInPosition, trackInSize;
        juce::AudioProcessorGraph::Node* instrumentNode = nullptr;
        juce::String instrumentModuleType, instrumentUuid;
        juce::AudioProcessorGraph::Node* chainSource = nullptr;
        int sourceRightChannel = 1;
        juce::String chainSourceType;
        juce::Point<int> chainSourcePosition;
        juce::String voiceMixerUuid, polyMidiUuid, adsrUuid, vcaUuid;
    };
    bool createTrackInForInstrumentChain(int index, const juce::String& trackNamePrefix, juce::String& trackName,
                                         InstrumentChainBuild& build);
    bool adoptInstrumentNodeForChain(std::shared_ptr<std::unique_ptr<juce::AudioProcessor>> stagedInstrument, int index,
                                     bool poly, InstrumentChainBuild& build);
    void buildInstrumentEnvelopeChain(InstrumentChainBuild& build);
    bool buildInstrumentChannelAndMacro(const juce::String& trackName, InstrumentChainBuild& build);
    // Shared by the default-consulting branch in addAudioTrack/buildInstrumentTrackAndChain, the
    // "+ Track" preset-list click and "Insert Track Preset from File...". NO UNDO TRANSACTION OF
    // ITS OWN (the default-consulting branch is already inside addAudioTrack's own) — callers that
    // aren't already inside one wrap this in recordGraphTimelineAndMacroChange themselves. Returns
    // the created track's name, or an empty string on rejection/failure.
    juce::String insertTrackFromPresetVar(const juce::var& preset, synth::TrackPresetKind kind,
                                          const juce::String& trackNamePrefix);

    // FRO42: hosted-plugin instrument loads in flight — see addInstrumentPluginTrack's own comment
    // for why THIS (an external, independent owner) holds the staged processor rather than a
    // shared_ptr captured by the module's own onLoadCompleted callback, which would make the object
    // keep itself alive via its own member. A failed/refused load is dropped via
    // dropPendingInstrumentPluginLoad(), deferred to the next message-loop turn so the module is
    // never destroyed from inside its own currently-executing onLoadCompleted callback.
    std::vector<std::unique_ptr<juce::AudioProcessor>> pendingInstrumentPluginLoads_;
    void dropPendingInstrumentPluginLoad(juce::AudioProcessor* processor);

    void updateRoundTripLatencyReadout();

    void installHostedPluginObservers();

    void commitMidiRecording();

    // ---- Audio recording ----

    // Everything an armed-Audio-track take needs between the Record-on click and the commit. All
    // message-thread state.
    //
    // Capture starts at the click, so a take is either rolling or not — no separate "armed,
    // waiting for the punch" state. The punch is the earliest beat the COMMITTED CLIP may start
    // at; pre-roll frames are recorded and then trimmed out of the clip window.
    struct AudioTake {
        bool capturing = false;   // the tap is writing
        synth::TrackId track;     // the armed Audio track the clip lands on
        double punchInBeat = 0.0; // earliest beat the committed clip may start at (see above)
        juce::File wavFile;       // absolute path being written
        juce::File peaksFile;     // its .agpk sidecar
        juce::String assetRef;    // what the committed clip stores (see synth::Clip::assetRef)
        juce::AudioProcessorGraph::NodeID tapNode;

        // Rate/tempo/latency FROZEN at capture start, never re-read at commit: every recording
        // anchor is in THIS rate's sample domain, and a device/sample-rate change mid-take (which
        // forces an early commit - AudioEngine::handleStreamFormatChange) would otherwise have
        // commitAudioRecording() convert an old-rate anchor with the new rate. A no-op when the rate
        // never changes, which is the overwhelmingly common case.
        double captureSampleRate = 44100.0;
        double captureBpm = 120.0;
        int captureRecordingLatencySamples = 0;
    };

    juce::AudioProcessorGraph::Node* ensureMasterRecordTap();

    RecordTapModule* findMasterRecordTap() const;

    bool chooseTakeFiles(AudioTake& take) const;

    void commitAudioRecording();

    // RAII suspension of automation capture for the duration of a programmatic rewrite.
    struct ProgrammaticApplyScope {
        explicit ProgrammaticApplyScope(MainComponent& owner)
            : guard(owner.automationRecorder) {}
        synth::AutomationRecorder::ScopedProgrammaticApply guard;
    };

    // ---- TrackHeaderHost ----
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId forTrack) override;
    juce::String getNodeDisplayName(const juce::String& uuid) override;
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
    // FRO13 (P9-7, docs/mixer.md §5.7): track presets.
    bool canSaveTrackPresetForTrack(synth::TrackId track) const override;
    void saveTrackAsPreset(synth::TrackId track) override;
    void setTrackPresetAsDefault(synth::TrackId track) override;
    void addTrackFromPreset(const juce::String& presetName, synth::TrackPresetKind kind) override;
    void addTrackFromPresetFile() override;
    void makeChannelForNode(juce::AudioProcessorGraph::NodeID source);
    void duplicateIntoChannel(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& macroId);
    std::vector<synth::PluginIdentity> getInstrumentPluginOptions() const override;
    bool isPluginScanInProgress() const override { return getPluginScanService().isScanning(); }
    void ensureInstrumentPluginsScanned() override { maybeStartEagerPluginScan(); }
    void addInstrumentPluginTrack(const synth::PluginIdentity& identity) override;
    std::vector<synth::ui::TrackHeaderHost::PluginLaneOption> getAvailablePluginLaneOptions() const override;
    synth::LaneId addPluginAutomationLane(const synth::ui::TrackHeaderHost::PluginLaneOption& option) override;
    // The colour picker's favourites shelf persists here — the only TrackHeaderHost override
    // that isn't graph/timeline plumbing (see ColourPickerPopup.h).
    juce::ApplicationProperties* getAppProperties() override { return &appProperties; }
    std::vector<synth::ui::TrackHeaderHost::MidiDestinationOption>
    getMidiDestinationOptions(synth::TrackId forTrack) override;
    void setMidiDestinationConnected(synth::TrackId forTrack, juce::uint32 nodeUid, bool connect) override;
    void auditionTrackNote(synth::TrackId forTrack, int pitch, int velocity, bool noteOn) override;
    synth::ui::TrackChannelLinkSurface* getChannelLinkSurface() override { return &trackChannelLink_; }

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
    void promptExportAudio();
    void promptExportStems();
    void loadFactoryPresetAtIndex(int index);
    void loadPresetGuarded(int index);
    void openRecentProjectGuarded(const juce::File& file);
    void clearTimelineForNewPatch();
    void newPatch();
    void launchOpenPresetChooser();
    void launchOpenProjectChooser();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    void globalFocusChanged(juce::Component* focusedComponent) override;

    void initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);

    // ---- initialiseCommon()'s ordered setup steps (FRO76) ----
    // Bodies moved verbatim out of the former single-function initialiseCommon(); the call order in
    // initialiseCommon() IS the contract (see the ORDER comments at each call site) — these are
    // declared in that same order for the same reason. Defined across MainComponentSetup.cpp /
    // MainComponentSetupToolbar.cpp / MainComponentSetupTimeline.cpp.
    void restorePanelPreferences();
    void restoreGraphEditorPreferences();
    void configureAiProvider(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);
    void wireAiChatAndAccount();
    void wireGraphEditorCallbacks();
    void wirePluginScanAndRecents();
    void wireCommandsAndShortcuts();
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
    void createWelcomeScreen();
    void registerFocusRegions();
    // FRO12: the clear+rebuild half of registerFocusRegions(), re-run after every detach/redock
    // (mixerDock.onPanelDetachStateChanged) so a region currently detached to its own window stops
    // appearing in the DOCKED window's Tab-cycle order -- split out so the one-time
    // addFocusChangeListener(this) call in registerFocusRegions() itself never re-registers.
    void rebuildFocusRegions();

    void applyToolbarIcons();

    void applyStoredDualIOPreferenceToPatch();

    juce::String computeOutputDeviceInfoText() const;

    void setLibraryVisible(bool v);

    // ---- Welcome screen (T114/P8-10) ----

    void hideWelcomeScreen();
    void showWelcomeScreen();
    void showWhatsNewDialog();

    // ---- Timeline panel height (user-resizable, persisted) ----

    int defaultTimelinePanelHeight() const;

    int clampTimelinePanelHeight(int desiredHeight) const;

    void setTimelinePanelHeight(int desiredHeight, bool persist);

    void setCurrentPatchName(const juce::String& name);
    void markDocumentClean();
    bool isRecordingActive() const;
    void maybeAutosave();
    void performAutosave();
    void notifyDocumentTitleChanged();

    void promptUnsavedChanges(const juce::String& actionLabel, std::function<void(UnsavedChangesChoice)> onChoice);
    void applyUnsavedChangesAnswer(UnsavedChangesChoice choice, std::function<void()> proceed);

    // Owned fallback objects used when the delegating ctor is called (tests/legacy).
    // Null when the primary ctor is used (refs point at external objects instead).
    std::unique_ptr<synth::theme::ThemeManager> ownedThemeManager;
    std::unique_ptr<synth::theme::AppLookAndFeel> ownedLookAndFeel;

    // Non-owning references to the active ThemeManager and LookAndFeel.
    // Always valid — set by both constructors (either to external objects or to the
    // owned fallbacks above).
    synth::theme::ThemeManager* themeManager{nullptr};
    synth::theme::AppLookAndFeel* lookAndFeel{nullptr};

    // The app's ONE live timeline document, and the recorder that captures parameter gestures into
    // its automation lanes.
    //
    // DECLARATION ORDER IS LOAD-BEARING - both precede `undoManager` so both outlive it: a
    // TimelineSnapshotAction on the undo stack holds a reference to this doc, and to the recorder.
    synth::TimelineDoc timelineDoc;
    synth::AutomationRecorder automationRecorder;
    // The app's one live MidiRecorder — no lifetime constraint against undoManager the way
    // timelineDoc/automationRecorder have (it holds no reference to the doc or the undo manager
    // between calls; stopAndCommit() takes both as parameters), so ordering here is not load-bearing.
    synth::MidiRecorder midiRecorder;

    AppUndoManager undoManager;

    // Owned only on the standalone paths (both ctors that don't take an external engine).
    // Null when the plugin ctor injected the processor's engine — see the `audioEngine`
    // reference below, which is the single access point either way. Mirrors the
    // ownedThemeManager / themeManager split above.
    std::unique_ptr<AudioEngine> ownedAudioEngine;
    AudioEngine& audioEngine;

    GraphEditor graphEditor;

    // T114/P8-10: the startup overlay offering New/Open Default/Open Existing/Recent instead of
    // silently auto-loading the factory preset. Null in Hosted mode — see ownedAudioEngine's gate
    // in initialiseCommon(); a hosted plugin's document is host-owned, so it must never construct
    // or show this. Added to the component tree LAST (after every other addAndMakeVisible() call in
    // initialiseCommon()), so it paints on top of the toolbar/canvas while visible.
    std::unique_ptr<synth::ui::WelcomeScreenComponent> welcomeScreen_;

    // Every open hosted-plugin editor window. Declared AFTER ownedAudioEngine/audioEngine (and
    // graphEditor) so reverse-order destruction kills it FIRST: a window's content can hold a live
    // juce::AudioPluginInstance editor that must not outlive its graph node. ~MainComponent() also
    // calls closeAll() as its first line - a second, independent defence (see that class' comment).
    synth::HostedPluginWindowManager pluginWindowManager;

    ModuleLibraryComponent moduleLibrary;

    // Toolbar strip (paints the bg + lays out the 9 buttons below via FlexBox). The buttons
    // remain direct children of MainComponent so existing getChildren() accessors still work.
    ToolbarComponent toolbar;

    // The 10 toolbar buttons (9 actions + toggleLibrary). DrawableButton so they carry SVG
    // icons; ButtonParameterAttachment / .onClick wiring works on the juce::Button base.
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
    // Timeline panel toggle — see ToolbarComponent::Slot::ToggleTimeline.
    juce::DrawableButton toggleTimelineButton{"toggleTimeline", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton themeToggleButton{"toggleTheme", juce::DrawableButton::ImageAboveTextLabel};

    std::unique_ptr<juce::FileChooser> fileChooser;

    // Declared BEFORE aiChatComponent, whose constructor reads a persisted setting straight out of
    // this (kDefaultRequestTimeoutMs): members construct in declaration order, and the other order
    // was UB (observed as a hang in juce::PropertySet::getIntValue). setStorageParameters() still
    // runs later, in initialiseCommon() - this fixes the crash, not the file-not-loaded-yet gap.
    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    synth::AIIntegrationService aiService;
    // Declared BEFORE aiChatComponent so reverse-order destruction tears the chat component down
    // first, while this is still alive to have the callback slots it installed (setAccountService)
    // cleared. P4-6: explicit production host - AccountService's own localhost:8787 default is a
    // dev convenience and MainComponent is the real composition root; a Debug build redirects via
    // AGENTSYNTH_LOCAL_API_URL (synth::branding::resolveApiBaseUrl()).
    synth::AccountService accountService{synth::branding::resolveApiBaseUrl()};
    synth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;
    bool isLibraryVisible{true};
    bool isAlignmentGuidesEnabled{true}; // NEW: default TRUE for backward compatibility

    // FRO14 (docs/mixer.md 5.2): the track <-> channel link, its own collaborator rather than more
    // methods here. Declared after the members it references. Contract: TrackChannelLinkController.h.
    synth::ui::TrackChannelLinkController trackChannelLink_{audioEngine, timelineDoc, undoManager, graphEditor};

    // Bottom-docked timeline panel shell.
    synth::ui::TimelinePanelComponent timelinePanel;
    // FRO11 (P9-5): the ONE member this ticket adds here (MainComponent.h's tight line budget --
    // see docs/mixer_implementation.md item 4). Takes timelinePanel by reference, constructed
    // after it in this same member list so the reference is valid; owns the tab strip and the
    // mixer panel itself, and becomes the dock's direct child in place of timelinePanel (which
    // becomes MixerDockComponent's own child instead -- see MainComponentSetupToolbar.cpp).
    // FRO12: appProperties/lookAndFeel/shortcutManager are declared earlier in this class (see
    // each field's own declaration) so all three are already valid pointers/references here,
    // even though shortcutManager itself finishes constructing later -- see
    // DetachablePanelHost.h's "held by reference" contract; only the ADDRESS is taken now.
    synth::ui::MixerDockComponent mixerDock{timelinePanel, audioEngine,   timelineDoc, undoManager,
                                            graphEditor,   appProperties, lookAndFeel, &shortcutManager};
    // FRO12 (P9-6, docs/mixer.md §5.9): Mixer placement (Tab/Own panel/Window) + both panels'
    // detach-to-window support -- ONE collaborator so this header doesn't grow a field per panel
    // (see the plan's own MainComponent.h budget note). Declared after mixerDock so its Mixer-
    // panel reference stays valid.
    synth::ui::MixerPlacementController mixerPlacement_{mixerDock, appProperties};
    bool isTimelineVisible = false;
    // The panel's docked height. Resolved in initialiseCommon() from kTimelinePanelHeightKey (theme
    // metric when absent) and moved by the panel's top-edge drag; 0 only before that, and forever in
    // a flag-OFF build, where nothing carries it into a layout.
    int timelinePanelHeight_ = 0;
    // Playing->stopped edge detection for the MIDI recorder's auto-commit-on-stop, updated
    // once per 10 Hz poll tick — mirrors AutomationRecorder's own `lastPlaying` bookkeeping.
    bool wasTransportPlaying_ = false;

    // The feedback-guard re-arm latch. True from a guard trip until the explicit reset
    // gesture — the armed-Audio-track set going from NONE armed to at least one armed again (disarm
    // then re-arm). While true, the poll keeps input monitoring off even though an Audio track is
    // still armed; simply staying armed must not re-enable it.
    bool feedbackGuardLatched_ = false;
    // Previous poll's "is any Audio-kind track armed" result — the FALSE -> TRUE edge is what
    // clears the latch above.
    bool wasAnyAudioTrackArmed_ = false;

    // The in-flight audio take (see the AudioTake declaration above).
    AudioTake audioTake_;
    // The bundle this document was last saved to or opened from, or an invalid File for a project
    // that has never been saved. Decides where a take is written (see chooseTakeFiles).
    juce::File currentBundleDir_;

    // Open programmatic-apply scopes for the undo/redo restore span, as a stack rather than a
    // single slot: an undo of a COMBINED (graph + timeline) change performs two restores, and the
    // AppUndoManager hooks that push/pop these are called around each of them.
    std::vector<std::unique_ptr<ProgrammaticApplyScope>> programmaticApplyScopes;

    // The AI apply's span: opened in aiPatchAboutToApply, closed in aiPatchApplied. Kept in its own
    // slot rather than on the stack above because the pair is NOT guaranteed balanced — an apply
    // whose applyJSONToGraph fails never fires aiPatchApplied (see AIIntegrationService::applyNow)
    // — and assigning a new scope over an abandoned one closes it, so a failed apply cannot leave
    // capture suspended for longer than until the next apply.
    std::unique_ptr<ProgrammaticApplyScope> aiApplyScope;

    // Cached narrow-mode state — applyToolbarIcons() re-clones icons ONLY on the transition.
    bool toolbarNarrowMode_{false};

    // Status-bar polling gate: timerCallback() runs at 10 Hz; the status bar updates at 5 Hz
    // (every 2nd tick).
    int statusBarTickCount_{0};

    // Declared BEFORE statusBar so it is fully constructed when statusBar's ctor runs.
    juce::String currentPatchName_{"Default"};
    StatusBarComponent statusBar;
    // True once an undo-able edit has happened since the last save/load — recomputed by
    // changeListenerCallback's AppUndoManager branch, cleared through markDocumentClean() by
    // saveToFile/openFromFile/newPatch. NOT by loadFactoryPresetAtIndex, which keeps the live
    // timeline and so has no right to claim the document matches anything on disk.
    // Never touched by exportPatchOnly (a side export, not "the project got saved").
    bool isDirty_ = false;
    // The AppUndoManager::getEditSerial() value as of the last save/load/new document — the
    // baseline isDirty_ is derived from. See markDocumentClean() for why a serial rather than just
    // the flag: the undo manager's change broadcast is async, so a notification can arrive after
    // the document was reset and must be able to recompute rather than blindly re-dirty it.
    int savedEditSerial_ = 0;

    // FRO42: bumped once by guardUnsavedChanges() immediately before it runs `proceed` - never on
    // Cancel or a failed Save arm. addInstrumentPluginTrack captures it when an async hosted-plugin
    // load starts (pendingInstrumentPluginLoads_) and its completion compares it against the live
    // value before building a track: a mismatch means the document that load belonged to is gone, so
    // the completion is dropped (graph/undo untouched).
    int documentGeneration_ = 0;

    // Autosave's own baseline — a SEPARATE serial from savedEditSerial_ above (see
    // maybeAutosave()/performAutosave()/markDocumentClean() comments): rebased on a successful
    // autosave write and on markDocumentClean(), never on anything else. Comparing against this
    // directly (rather than isDirty_) is what stops autosave from rewriting an unchanged sidecar
    // every interval forever.
    int lastAutosavedEditSerial_ = 0;
    // juce::Time::getMillisecondCounter() as of the last autosave write (or the last
    // markDocumentClean(), which resets this so a freshly opened/saved document doesn't autosave on
    // its very first qualifying tick). Wall-clock rather than a tick count on purpose: the shared
    // 10 Hz timer's actual firing rate is not guaranteed exact.
    juce::uint32 lastAutosaveMs_ = 0;

    // True while an Export Audio (bounce) OR Export Stems render is in flight - checked by
    // maybeAutosave() and guardUnsavedChanges(), neither of which may touch the document while the
    // engine is offline-prepared (see BounceRunner.h/StemRunner.h). ONE flag for both, deliberately:
    // the offline render path is exclusive across the two, not per-kind.
    bool isBounceInProgress_ = false;
    std::unique_ptr<synth::BounceRunner> bounceRunner_;
    std::unique_ptr<synth::StemRunner> stemRunner_;
    // The currently-shown Export Audio/Export Stems dialog, polled for progress by timerCallback().
    // A SafePointer because the modal window can go away independently and reportProgress() must
    // become a no-op rather than dangle; owned by its DialogWindow, never by MainComponent. Shared
    // by both flows - only one of bounceRunner_/stemRunner_ is ever non-null at a time.
    juce::Component::SafePointer<synth::ui::ExportAudioDialog> exportDialog_;

    // Declared here (not in AudioEngine or Core) because it is settings-backed and
    // UI-driven; installed into the process-wide DefaultHostedPluginBackend by the constructor and
    // uninstalled by the destructor, so a HostedPluginModule restoring a patch can resolve its
    // identity without anything having to plumb a backend down through applyJSONToGraph.
    synth::PluginScanService pluginScanService;

    // The service actually in use — ours, or the one already installed on the backend when this
    // editor was built on an external engine (the plugin path: it belongs to the processor, which
    // outlives every editor). Never null.
    synth::PluginScanService* activeScanService = &pluginScanService;

    // The Load menu's "Recent Projects" section — settings-backed, single owner (see
    // kRecentProjectsSettingKey's comment), restored on startup and rewritten after every
    // successful bundle save/open.
    synth::RecentProjects recentProjects;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    // Consulted first by resolveEditSurface(); std::nullopt means "use real focus".
    std::optional<EditSurface> editSurfaceOverrideForTest_;

    // T159: the focus-region registry (Source/UI/Layout/FocusRegion.h) — a plain member, not a
    // Desktop-global singleton, so a future separate-window mixer/timeline gets its own instance.
    // Populated once in initialiseCommon() after every region root exists; wraps the same
    // isLibraryVisible/isTimelineVisible/isAiPanelVisible/isModMatrixVisible getters the toolbar
    // toggles already use rather than migrating them to a new unified enum.
    synth::ui::FocusRegionRegistry focusRegions_;

#if JUCE_MAC || JUCE_WINDOWS
    synth::update::UpdateManager updateManager;
#endif

    // ---- Panel slide animations (fraction-driven, time-bounded, auto-stop) ----
    //
    // Each sliding panel owns a [0..1] open fraction and resized() derives its size from that, so a
    // layout pass is correct whenever it runs (docs/layout.md §11). ONE driver moves ALL THREE: the
    // panels share a window, so a per-panel animator would leave one slide frozen half-open.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver panelSlideAnim_;
    synth::ui::PanelSlide librarySlide_;
    synth::ui::PanelSlide aiPanelSlide_;
    synth::ui::PanelSlide timelineSlide_;

    /** ~190 ms, inside the house 160–220 ms spec (docs/layout.md §11) — the duration the panels
     *  have always slid for, now shared by all three of them. */
    static constexpr double kPanelSlideMs = 190.0;

    const synth::ui::PanelSlide& panelSlide(SlidingPanel p) const noexcept;
    synth::ui::PanelSlide& panelSlide(SlidingPanel p) noexcept;

    /** THE panel-toggle seam: point every slide at the current visibility flags and run one
     *  coordinated tween — or land immediately when nothing can animate (an off-screen component
     *  gets no VBlank, so a headless toggle must be synchronous; that is the contract
     *  Tests/UI/Layout/PanelAnimationAndLoadingTests.cpp asserts with no message pump at all).
     *  Callers flip the flag, persist it, refresh the toolbar, then call this. */
    void beginPanelSlide();

    void applyPanelSlideFrame(float t);

    void finishPanelSlide();

    void setAlignmentGuidesEnabled(bool enabled);

    // Provides native-style tooltips for any child Component that has a tooltip
    // string set via setTooltip(). Constructed last so all child components exist.
    // Do NOT set tooltips on controls here; each feature owner does that.
    juce::TooltipWindow tooltipWindow{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
