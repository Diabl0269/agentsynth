#pragma once

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AccountService.h"
#include "AppUndoManager.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
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
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/Timeline/TimelinePanelComponent/TimelinePanelComponent.h"
#include "UI/Timeline/TimelineTrackHeaderComponent.h"
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

    // Headless tests can't always create a real keyboard-focus grab (grabKeyboardFocus() needs a
    // native peer — see FocusArbitrationTests.cpp's SurfaceResolverRealFocus for why this repo
    // doesn't attempt one). Consulted FIRST, before any real-focus check; std::nullopt (the
    // default) falls through to that check.
    void setEditSurfaceOverrideForTest(std::optional<EditSurface> surface) { editSurfaceOverrideForTest_ = surface; }

    bool performRepeatSelection(int count);

    /** Repeat's count bounds, shared by the dialog's clamp and performRepeatSelection's own. 64 is
     *  a deliberate ceiling rather than a technical one: every copy is a real doc mutation inside a
     *  single undo transaction, and a mistyped four-digit count would otherwise stall the message
     *  thread building clips nobody asked for. */
    static constexpr int kMinRepeatCount = 1;
    static constexpr int kMaxRepeatCount = 64;

    // P4-6: pure decision function for the AI provider id used when no "aiProvider" key is
    // persisted yet. A brand new install (no pre-existing settings file at all) defaults to
    // "remote" (hosted); an install that has launched before but never touched AI settings — the
    // common case, since that key is only ever written by AISettingsTab::updateSettings() —
    // keeps its working "ollama" default rather than being silently moved to hosted on upgrade.
    // Extracted as a free function so this decision is unit-testable without touching a real
    // properties file — see initialiseCommon() for the caller and the existsAsFile() check it's
    // based on.
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

    /** Panel-slide test seams, mirroring PianoRollComponent's scale-panel ones (and used the same
     *  way: the component is never added to a real window in the test suite, so every toggle takes
     *  the SNAP path — the tween's own per-frame geometry is exercised by writing a fraction here
     *  and reading the resulting bounds back).
     *
     *  The fractions ARE the layout: resized() derives every panel's size from them (see
     *  synth::ui::PanelSlide), so setPanelOpenProgressForTest() also re-lays-out. */
    float getPanelOpenProgressForTest(SlidingPanel p) const noexcept { return panelSlide(p).getProgress(); }
    void setPanelOpenProgressForTest(SlidingPanel p, float progress) {
        panelSlide(p).snapTo(progress);
        resized();
    }
    /** The fraction the in-flight tween STARTED from — 'no jump' means this is the panel's
     *  mid-slide value at the moment of the re-toggle, never 0 or 1. */
    float getPanelSlideStartForTest(SlidingPanel p) const noexcept { return panelSlide(p).getTweenStart(); }
    /** True only while the shared slide driver is actually running (it auto-stops at t == 1 —
     *  there is exactly one driver for all three panels). */
    bool isPanelSlideAnimatingForTest() const noexcept { return panelSlideAnim_.isRunning(); }
    /** The Preferences "Natural scrolling" key. DEFAULT TRUE (natural) — the value every scrolling
     *  surface in the app already behaves as, so an install that never opens Preferences is
     *  unaffected. Owned here rather than by the tab because MainComponent is what applies it. */
    static constexpr const char* kNaturalScrollingKey = "naturalScrolling";

    /** Re-reads kNaturalScrollingKey and pushes `!natural` into the two surfaces that have an
     *  inversion flag (the timeline panel and its piano roll — the graph canvas pans rather than
     *  scrolls). Called once at startup and again on every settings-file change, which is how a
     *  Preferences toggle reaches the panel live: PreferencesSettingsTab writes the key, the
     *  PropertiesFile broadcasts, and changeListenerCallback lands here. Idempotent, so being
     *  called for an unrelated settings write costs a bool read. */
    void applyNaturalScrollingPreference();

    /** The Preferences "Scroll up to zoom in" checkbox's key (relabelled from "Scroll up zooms in"
     *  in round 3; briefly a "Zoom direction" two-option dropdown in round 5, reverted back to a
     *  checkbox in round 6 after user pushback on two-value selects). The persisted key name and
     *  its boolean semantics have been unchanged, migration-free, across every one of those rounds.
     *  DEFAULT TRUE: "up zooms in" is what both wheel-zoom surfaces already did before the
     *  preference existed, so an install that never opens Preferences is unaffected. Owned here for
     *  the same reason kNaturalScrollingKey is: the tab writes it, MainComponent is what applies
     *  it. */
    static constexpr const char* kZoomScrollUpZoomsInKey = "zoomScrollUpZoomsIn";

    /** Re-reads kZoomScrollUpZoomsInKey and pushes `!upZoomsIn` into the timeline panel, which
     *  forwards it to the piano roll. A SIBLING of applyNaturalScrollingPreference rather than an
     *  extension of it, because the two preferences are genuinely independent: plain-scroll direction
     *  and wheel-ZOOM direction are separate flags on both surfaces (see
     *  TimelinePanelComponent::setZoomScrollInverted), and a user who inverts one has said nothing
     *  about the other. Shares that function's propagation path exactly — the settings-file
     *  ChangeBroadcaster — so both are called from the constructor and from
     *  changeListenerCallback, and both are idempotent. */
    void applyZoomScrollPreference();

    /** Per-press zoom step for the four zoom commands. 1.25 is the same "a quarter bigger" feel a
     *  couple of wheel notches gives, and the out factor is its exact reciprocal so in-then-out
     *  returns to where you started rather than drifting. */
    static constexpr double kZoomInFactor = 1.25;
    static constexpr double kZoomOutFactor = 1.0 / kZoomInFactor;
    bool isTimelineConfiguredVisible() const { return isTimelineVisible; }
    synth::ui::TimelinePanelComponent& getTimelinePanel() { return timelinePanel; }

    /** The settings key the user-dragged timeline height round-trips through. The theme metric
     *  (Metrics::timelinePanelHeight) is only the DEFAULT — see clampTimelinePanelHeight(). */
    static constexpr const char* kTimelinePanelHeightKey = "timelinePanelHeight";

    /** The panel's current docked height in px, always clamped (see clampTimelinePanelHeight()). */
    int getTimelinePanelHeight() const noexcept { return timelinePanelHeight_; }
    // Test hooks. The doc and the recorder are real app state (not test-only objects), so
    // these are plain accessors; the simulate*/…ForTest entry points below drive the same code
    // paths the buttons and file dialogs do, minus the dialogs.
    synth::TimelineDoc& getTimelineDoc() { return timelineDoc; }
    synth::AutomationRecorder& getAutomationRecorder() { return automationRecorder; }
    // Right-click-any-knob's headless hook, and the production entry point
    // GraphEditor::onAutomateParameterRequested is wired to. Resolves `nodeId`'s uuid (assigning
    // one if it has none yet — the same ensure-uuid idiom createTrackInNode() uses), finds-or-
    // creates the doc's Automation-kind track, binds a lane for `paramId` with the parameter's real
    // NormalisableRange, and opens the timeline panel's automation strip on it. A no-op (with a
    // status-bar message) if `nodeId` doesn't resolve to a live ModuleBase or `paramId` doesn't
    // resolve to a real parameter on it.
    void automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    // The app's one live MidiRecorder — see docs/architecture.md's MidiRecorder wiring
    // entry. Test-only access mirrors getAutomationRecorder() above.
    synth::MidiRecorder& getMidiRecorderForTest() { return midiRecorder; }
    // The "+ Track" button opens a MIDI/Audio menu rather than adding a track outright, and a
    // juce::PopupMenu never runs in a test process — so these drive the menu's own headless seam
    // (TimelinePanelComponent::applyAddTrackMenuChoice), which is exactly what the async callback
    // calls when the user picks an item.
    void simulateAddMidiTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    }
    void simulateAddAudioTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }
    /** T183 (P9-3b): drives the Instrument submenu's headless seam directly, by menu id, so a test
     *  can pick which of the three instrument entries to simulate. */
    void simulateAddInstrumentTrackClick(int menuId) { timelinePanel.applyAddTrackMenuChoice(menuId); }
    /** Exactly what the Save dialog's callback runs: a name ending in `.agsproj` writes a project
     *  bundle (graph + timeline), anything else writes a plain `.json` preset. */
    bool saveProjectForTest(const juce::File& file) { return saveToFile(file); }
    /** The post-guard half of New Patch only — bypasses guardUnsavedChanges, same idiom as
     *  saveProjectForTest bypassing the save chooser. Tests that want the guard itself go through
     *  the AppCommands::newPatch command or unsavedChangesPrompt instead. */
    void newPatchForTest() { newPatch(); }
    /** Exactly what the Open dialog's callback runs: an `.agsproj` bundle directory loads graph +
     *  timeline, anything else loads a plain `.json` preset. If the bundle carries a pending
     *  autosave sidecar, this kicks off the async recovery prompt instead (see
     *  autosaveRecoveryPrompt) and returns true without the load having happened yet — a test
     *  exercising that path drives autosaveRecoveryPrompt directly, the same idiom
     *  unsavedChangesPrompt uses. */
    bool openProjectForTest(const juce::File& file) { return openFromFile(file); }
    // P8-31: reaches openFromFile's PATCH branch with an explicit load mode, so a test can drive the
    // replace-vs-append behaviour without a native file chooser (`append == true` adds onto the live
    // graph; false replaces it).
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
    // The docked AI chat panel — same plain-accessor role getGraphEditor()/getTimelinePanel() play
    // (the panel-slide tests read its bounds mid-slide).
    synth::AIChatComponent& getAiChatComponent() { return aiChatComponent; }
    ShortcutManager& getShortcutManager() { return shortcutManager; }
    // T159: the registry itself is plain-old-data/pure-logic (see FocusRegion.h), so tests exercise
    // it directly rather than through a maze of test-only wrapper methods here.
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

    /** What the user picked in the unsaved-changes dialog. Save runs performSaveProject and only
     *  continues if the save actually succeeded; Discard continues immediately; Cancel abandons the
     *  action that asked. */
    enum class UnsavedChangesChoice { Save, Discard, Cancel };

    /** Test/automation seam for the unsaved-changes dialog, same idiom as onDocumentTitleChanged:
     *  when set it REPLACES the real async juce::AlertWindow, so a headless run (which has no message
     *  loop to answer a real modal with) can drive whichever arm it wants. The first argument is the
     *  human-readable name of the action that is about to discard the document ("New Patch", "Quit"). */
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
    // Mirrors the loadButton factory-preset call site exactly (load + patch-name update), so
    // tests can verify the patch-name side effect without driving the async PopupMenu.
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
    // MainComponent owns a scan list because Core must not read or write settings, and scanning is a
    // standalone-app affair: a scan is refused outright when the engine is Hosted, so a DAW session
    // can never trigger a nested plugin scan.
    //
    // It is not always the list in use. On the plugin path AgentSynthAudioProcessor restores and
    // installs its OWN service before any editor exists (a host restores a session without ever
    // opening our window), and that service outlives every editor — so an editor started on an
    // external engine ADOPTS whatever is already installed instead of replacing it. Everything below
    // therefore goes through getPluginScanService(), never the member directly.

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

        // The transport's rate/tempo and the engine's round-trip latency, frozen at the
        // moment the capture started — NOT re-read at commit time. Recording anchors
        // (captureStartTimelineSample, the WAV itself) are all in THIS rate's sample domain; a
        // device/sample-rate change mid-take (which forces an early commit — see
        // AudioEngine::handleStreamFormatChange) would otherwise leave commitAudioRecording() reading
        // the engine's CURRENT (post-change) sampleRate/bpm/latency to convert an anchor that was
        // captured under the OLD ones, silently mixing two rates into one beat conversion. Freezing
        // these here makes the commit rate-independent unconditionally — a no-op for the (overwhelmingly
        // common) case where the rate never changes during a take, since these values never differ
        // from the live ones then.
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

    // Shared initialisation body called from both constructors after appProperties is set up.
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
    // The Load button's popup menu body, extracted out of addFileButtons() (FRO76).
    void showLoadMenu();
    void addUndoButtons();
    void wireTimelinePanel();
    void wireTimelinePanelServicesAndShortcuts();
    void wireTimelineHookInventory();
    void wireTimelineClipLaneCallbacks();
    void wireTimelineRecordToggle();
    // The transport bar's onRecordToggled body, extracted out of wireTimelineRecordToggle() (FRO76).
    void handleRecordToggle(bool wantRecording);
    void addToolbarToggleButtons();
    void assembleToolbar();
    void wireStatusBar();
    // Returns false exactly where initialiseCommon() used to `return;` early on the plugin path
    // (ownedAudioEngine == nullptr) — the caller mirrors that with `if (!initialiseAudioEngine())
    // return;`. True means the rest of initialiseCommon() (welcome screen, focus regions) still runs.
    bool initialiseAudioEngine();
    void createWelcomeScreen();
    void registerFocusRegions();

    void applyToolbarIcons();

    void applyStoredDualIOPreferenceToPatch();

    juce::String computeOutputDeviceInfoText() const;

    // Collapse/expand the library sidebar. Slides to the target layout (beginPanelSlide()).
    void setLibraryVisible(bool v);

    // ---- Welcome screen (T114/P8-10) ----

    void hideWelcomeScreen();
    void showWelcomeScreen();
    void showWhatsNewDialog();

    // ---- Timeline panel height (user-resizable, persisted) ----

    int defaultTimelinePanelHeight() const;

    int clampTimelinePanelHeight(int desiredHeight) const;

    void setTimelinePanelHeight(int desiredHeight, bool persist);

    // Update the displayed patch name (status bar). Immediate repaint, no timer delay.
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

    // The app's ONE live timeline document, and the recorder that captures parameter
    // gestures into its automation lanes.
    //
    // DECLARATION ORDER IS LOAD-BEARING — both are declared before `undoManager`, so both outlive
    // it: a TimelineSnapshotAction sitting on the undo stack holds a reference to this doc (see
    // AppUndoManager::recordTimelineChange), and members are destroyed in reverse declaration
    // order. The recorder likewise must not be destroyed while an undo action could still commit
    // into it.
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

    // Every open hosted-plugin editor window. Declared AFTER ownedAudioEngine/audioEngine
    // (and graphEditor) so it is destroyed BEFORE them — members are torn down in REVERSE
    // declaration order, and a window's content can hold a live juce::AudioPluginInstance editor
    // that must not outlive the graph node it came from. ~MainComponent() ALSO calls
    // pluginWindowManager.closeAll() explicitly, as its very first line — a second, independent
    // line of defence against the same hazard; see HostedPluginWindowManager's class comment for
    // why both exist. Declaration-order safety alone would still work if that explicit call were
    // ever accidentally removed.
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

    // Declared BEFORE aiChatComponent: its constructor reads a persisted setting straight out of
    // appProperties (see AIChatComponent's kDefaultRequestTimeoutMs restore), and members are
    // constructed in declaration order regardless of initializer-list order — appProperties being
    // any later than aiChatComponent left that read touching a not-yet-constructed object (UB,
    // observed as a hang inside juce::PropertySet::getIntValue). setStorageParameters() itself
    // still runs later, in the constructor body (initialiseCommon()'s ORDERING CONTRACT re-syncs
    // whatever the too-early read missed) — this fixes the crash, not the file-not-loaded-yet gap.
    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    synth::AIIntegrationService aiService;
    // Declared BEFORE aiChatComponent: members are destroyed in reverse declaration order, so
    // aiChatComponent (which installs AccountService::onStateChanged/onAccessTokenChanged in
    // setAccountService(), see its header comment) is torn down first, while accountService is
    // still alive to have those callback slots cleared.
    // P4-6: explicit production host — the AccountService(host) default of localhost:8787 is a
    // dev/test convenience only, and MainComponent is the real composition root. A Debug build can
    // still redirect this to a local synth-platform server via AGENTSYNTH_LOCAL_API_URL — see
    // synth::branding::resolveApiBaseUrl().
    synth::AccountService accountService{synth::branding::resolveApiBaseUrl()};
    synth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;
    bool isLibraryVisible{true};
    bool isAlignmentGuidesEnabled{true}; // NEW: default TRUE for backward compatibility

    // Bottom-docked timeline panel shell.
    synth::ui::TimelinePanelComponent timelinePanel;
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

    // FRO42 review fix: bumped exactly once by guardUnsavedChanges() immediately before it actually
    // runs `proceed` — i.e. once per New Patch/Open/Load-preset/Quit that really goes ahead, never
    // on Cancel or a failed/cancelled Save arm. A hosted-plugin instrument load started against the
    // PREVIOUS document is asynchronous (see pendingInstrumentPluginLoads_); addInstrumentPluginTrack
    // captures this value when the load starts, and its onLoadCompleted lambda compares it against
    // the live value before building a track — a mismatch means the document underneath that load
    // is already gone, so the completion is dropped (graph/undo untouched) instead of landing a new
    // track in the freshly loaded/created document.
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

    // True from the moment an Export Audio (bounce) OR Export Stems render starts until its
    // completion callback runs — checked by maybeAutosave() (a sidecar write mid-render is
    // pointless and autosave has no business touching the document while the engine is
    // offline-prepared) and by guardUnsavedChanges() (New Patch/Open/Load preset/Quit must not
    // mutate or replace the graph out from under a live render — see BounceRunner.h/StemRunner.h).
    // ONE flag, deliberately, for both: the offline render path (suspendDeviceCallback + reprepare)
    // is exclusive regardless of which of the two is running, so "one render in flight at a time"
    // has to mean across both, not per-kind.
    bool isBounceInProgress_ = false;
    std::unique_ptr<synth::BounceRunner> bounceRunner_;
    std::unique_ptr<synth::StemRunner> stemRunner_;
    // The currently-shown Export Audio/Export Stems dialog, polled for progress by timerCallback() -
    // a SafePointer because the modal window (and its content) can go away independently, and
    // reportProgress() must simply become a no-op rather than a dangling call. Owned by the
    // DialogWindow that shows it, never by MainComponent. Shared by both flows (ExportAudioDialog's
    // stems mode is the same class) since only one of bounceRunner_/stemRunner_ is ever non-null at
    // a time.
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
    // Each sliding panel owns a [0..1] open fraction; resized() derives its size from that
    // fraction, so a layout pass is correct whenever it runs and a toggle only has to move the
    // fraction (docs/layout.md §11). ONE driver moves ALL THREE fractions — the panels share a
    // window, so their slides must share a clock: a per-panel animator would leave whichever
    // slide the next toggle didn't mention frozen half-open.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver panelSlideAnim_;
    synth::ui::PanelSlide librarySlide_;
    synth::ui::PanelSlide aiPanelSlide_;
    synth::ui::PanelSlide timelineSlide_;

    /** ~190 ms, inside the house 160–220 ms spec (docs/layout.md §11) — the duration the panels
     *  have always slid for, now shared by all three of them. */
    static constexpr double kPanelSlideMs = 190.0;

    // The three fractions, addressed by name (test seams above; nothing else needs this).
    const synth::ui::PanelSlide& panelSlide(SlidingPanel p) const noexcept;
    synth::ui::PanelSlide& panelSlide(SlidingPanel p) noexcept;

    /** THE panel-toggle seam: point every slide at the current visibility flags and run one
     *  coordinated tween — or land immediately when nothing can animate (an off-screen component
     *  gets no VBlank, so a headless toggle must be synchronous; that is the contract
     *  Tests/UI/Layout/PanelAnimationAndLoadingTests.cpp asserts with no message pump at all).
     *  Callers flip the flag, persist it, refresh the toolbar, then call this. */
    void beginPanelSlide();

    // Per-frame body of the slide above: advance all three fractions, then re-lay-out.
    void applyPanelSlideFrame(float t);

    // End of the slide (its completion callback, and the synchronous path's whole body): stop the
    // driver, pin the exact end fractions, hide whatever finished closing, lay out.
    void finishPanelSlide();

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled);

    // Provides native-style tooltips for any child Component that has a tooltip
    // string set via setTooltip(). Constructed last so all child components exist.
    // Do NOT set tooltips on controls here; each feature owner does that.
    juce::TooltipWindow tooltipWindow{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
