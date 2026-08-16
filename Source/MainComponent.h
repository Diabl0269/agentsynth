#pragma once

#include "AI/AIIntegrationService.h"
#include "AI/AIProviderRegistry.h"
#include "AI/AccountService.h"
#include "AppUndoManager.h"
#include "AudioEngine.h"
#include "Branding.h"
#include "Modules/RecordTapModule.h"
#include "PresetManager.h"
#include "ShortcutManager.h"
#include "SnippetManager.h"
#include "Timeline/AutomationRecorder.h"
#include "Timeline/MidiRecorder.h"
#include "Timeline/TimelineDoc.h"
#include "Timeline/TimelineOps.h"
#include "UI/AIChatComponent.h"
#include "UI/GraphEditor.h"
#include "UI/ModuleLibraryComponent.h"
#include "UI/StatusBarComponent.h"
#include "UI/Theme/AppLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include "UI/TimelinePanelComponent.h"
#include "UI/TimelineTrackHeaderComponent.h"
#include "UI/ToolbarComponent.h"
#include "UI/UIAnimation.h"
#include "Update/UpdateManager.h"
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
    // TL5-3: the app owns the one live TimelineDoc, so it is also the thing that republishes it to
    // the audio thread on every edit (timelineChanged) and the thing the track headers ask to
    // create/re-bind/delete their Track In nodes (TrackHeaderHost).
    , private synth::TimelineDoc::Listener
    , private synth::ui::TrackHeaderHost {
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

    bool keyPressed(const juce::KeyPress& key) override;

    juce::ApplicationCommandManager& getCommandManager() { return commandManager; }
    void updateCommandShortcuts();

    // ---- TL5-10: keyboard/focus arbitration ----
    // Which surface currently owns Cmd+C/V/D (Space's togglePlayback is deliberately
    // surface-independent — see ShortcutManager's binding comment and docs/shortcuts.md).
    // TimelineClips/PianoRoll require BOTH the timeline panel to be visible AND real keyboard
    // focus (juce::Component::getCurrentlyFocusedComponent()) to sit inside the clip-lane area /
    // piano roll respectively — a hidden panel never owns the verbs, whatever a stale focus
    // pointer points at. Every one of those surfaces already grabs focus on mouseDown (the canvas
    // idiom GraphEditor::mouseDown established, followed by TimelineClipLaneArea/
    // PianoRollComponent/AutomationLaneEditor), so "last-clicked surface owns the verbs" falls out
    // of ordinary JUCE focus tracking with no extra bookkeeping in this class. Public: both
    // perform()/getCommandInfo() and FocusArbitrationTests.cpp call it directly.
    enum class EditSurface { Graph, TimelineClips, PianoRoll };
    EditSurface resolveEditSurface() const;

    // Headless tests can't always create a real keyboard-focus grab (grabKeyboardFocus() needs a
    // native peer — see FocusArbitrationTests.cpp's SurfaceResolverRealFocus for why this repo
    // doesn't attempt one). Consulted FIRST, before any real-focus check; std::nullopt (the
    // default) falls through to that check.
    void setEditSurfaceOverrideForTest(std::optional<EditSurface> surface) { editSurfaceOverrideForTest_ = surface; }

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
#if SYNTH_ENABLE_TIMELINE
    void simulateToggleTimelineClick() {
        if (toggleTimelineButton.onClick)
            toggleTimelineButton.onClick();
    }
#endif
    bool isTimelineConfiguredVisible() const { return isTimelineVisible; }
    synth::ui::TimelinePanelComponent& getTimelinePanel() { return timelinePanel; }
    // TL5-3 test hooks. The doc and the recorder are real app state (not test-only objects), so
    // these are plain accessors; the simulate*/…ForTest entry points below drive the same code
    // paths the buttons and file dialogs do, minus the dialogs.
    synth::TimelineDoc& getTimelineDoc() { return timelineDoc; }
    synth::AutomationRecorder& getAutomationRecorder() { return automationRecorder; }
    // TL5-9: right-click-any-knob's headless hook, and the production entry point
    // GraphEditor::onAutomateParameterRequested is wired to. Resolves `nodeId`'s uuid (assigning
    // one if it has none yet — the same ensure-uuid idiom createTrackInNode() uses), finds-or-
    // creates the doc's Automation-kind track, binds a lane for `paramId` with the parameter's real
    // NormalisableRange, and opens the timeline panel's automation strip on it. A no-op (with a
    // status-bar message) if `nodeId` doesn't resolve to a live ModuleBase or `paramId` doesn't
    // resolve to a real parameter on it.
    void automateParameter(juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId);
    // TL5-5: the app's one live MidiRecorder — see docs/architecture.md's MidiRecorder wiring
    // entry. Test-only access mirrors getAutomationRecorder() above.
    synth::MidiRecorder& getMidiRecorderForTest() { return midiRecorder; }
    // TL6-4: the "+ Track" button opens a MIDI/Audio menu rather than adding a track outright, and a
    // juce::PopupMenu never runs in a test process — so these drive the menu's own headless seam
    // (TimelinePanelComponent::applyAddTrackMenuChoice), which is exactly what the async callback
    // calls when the user picks an item.
    void simulateAddMidiTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddMidiTrackMenuId);
    }
    void simulateAddAudioTrackClick() {
        timelinePanel.applyAddTrackMenuChoice(synth::ui::TimelinePanelComponent::kAddAudioTrackMenuId);
    }
    /** Exactly what the Save dialog's callback runs: a name ending in `.agsproj` writes a project
     *  bundle (graph + timeline), anything else writes a plain `.json` preset. */
    void saveProjectForTest(const juce::File& file) { saveToFile(file); }
    /** Exactly what the Open dialog's callback runs: an `.agsproj` bundle directory loads graph +
     *  timeline, anything else loads a plain `.json` preset. */
    bool openProjectForTest(const juce::File& file) { return openFromFile(file); }
    /** TL6-6: exactly what the production "Relink audio…" FileChooser callback runs once the user
     *  has picked a file — bypasses the async dialog itself, same idiom as saveProjectForTest. */
    void relinkClipAssetForTest(synth::ClipId id, const juce::File& chosenFile) { relinkClipAsset(id, chosenFile); }
    /** TL6-6: sweeps `<bundle>/Audio/` (+ `Peaks/`) for files no clip in the live timeline
     *  references and deletes exactly those — see synth::AssetManager::cleanUnusedAssets. A no-op
     *  (returns 0) outside a saved bundle. Not wired to any menu/shortcut yet — see
     *  docs/architecture.md's asset-management subsection for why. */
    int cleanUnusedAssetsForTest() { return cleanUnusedAssets(); }
    GraphEditor& getGraphEditor() { return graphEditor; }
    ToolbarComponent& getToolbar() { return toolbar; }
    StatusBarComponent& getStatusBar() { return statusBar; }
    ShortcutManager& getShortcutManager() { return shortcutManager; }
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
    // Non-const access to ApplicationProperties for persistence tests (read-back within session).
    juce::ApplicationProperties& getAppPropertiesForTest() { return appProperties; }
    int getStatusBarTickCountForTest() const { return statusBarTickCount_; }
    // Mirrors the loadButton factory-preset call site exactly (load + patch-name update), so
    // tests can verify the patch-name side effect without driving the async PopupMenu.
    void simulateLoadFactoryPresetForTest(int index);
    void openPresetFromFile();
    synth::AIIntegrationService& getAiServiceForTest() { return aiService; }

    // ---- Snippets (issue #156) ----

    /** Re-reads the snippets directory and pushes the list into the library sidebar. */
    void refreshSnippetLibrary();

    /** Asks for a name and saves the canvas selection as a snippet. No-op (with a status-bar
     *  note) when nothing is selected. */
    void promptSaveSnippet();

    ModuleLibraryComponent& getModuleLibrary() { return moduleLibrary; }

private:
    // AIIntegrationService::Listener
    void aiPatchAboutToApply() override;
    void aiPatchApplied() override;

    // ---- Timeline app wiring (TL5-3). Every body below is #if SYNTH_ENABLE_TIMELINE inside;
    //      a flag-OFF build compiles them as no-ops so no call site needs its own #if. ----

    // TimelineDoc::Listener — fired once per effective doc mutation. THE publish seam: republishes
    // the timeline to the audio thread and rebuilds the automation recorder's lane bindings.
    void timelineChanged(const synth::TimelineDoc& doc) override;

    // Publishes the doc to the engine and re-resolves the recorder's per-lane parameter bindings
    // against the CURRENT graph (the same uuid -> node -> parameter resolution
    // AudioEngine::publishTimeline does for the applier's binding table).
    void publishTimelineAndRebindRecorder();

    // Reconciles every track/lane binding against the live graph after a graph change that happened
    // outside a doc mutation (preset load, new patch, AI apply, undo/redo, bundle open). Publishes
    // ONLY when the reconcile itself changed nothing — a reconcile that flips a flag is a doc
    // mutation, so timelineChanged has already published by the time it returns.
    void reconcileTimelineAfterGraphChange();

    // The cheap half of the above, with no republish of its own: installed on
    // GraphEditor::onGraphStructureChanged as the catch-all for graph edits that have no explicit
    // post-apply site (a module deleted from the canvas). See the call site for why publishing
    // there would be waste.
    void reconcileTimelineBindingsOnly();

    // TL5-5: the ONE place a MIDI take ever commits — the transport bar's Record-off click and the
    // 10 Hz poll's auto-commit-on-stop (playing -> stopped while still recording) both route
    // through here, so the two paths can never diverge (one warns on overrun and flips the button
    // off, the other forgets to). A no-op (compiles to an empty body) with the flag off.
    void commitMidiRecording();

    // ---- TL6-3: audio recording ----

    // Everything an armed-Audio-track take needs between the Record-on click and the commit. All
    // message-thread state.
    //
    // TL6-8 removed the earlier `pending` state ("record engaged, waiting for the transport to reach
    // the punch"): the capture now starts at the click, so a take is either rolling or not. The
    // punch survives as the earliest beat the COMMITTED CLIP may start at — pre-roll frames are
    // recorded and then trimmed out of the clip window.
    struct AudioTake {
        bool capturing = false;   // the tap is writing
        synth::TrackId track;     // the armed Audio track the clip lands on
        double punchInBeat = 0.0; // earliest beat the committed clip may start at (see above)
        juce::File wavFile;       // absolute path being written
        juce::File peaksFile;     // its .agpk sidecar
        juce::String assetRef;    // what the committed clip stores (see synth::Clip::assetRef)
        juce::AudioProcessorGraph::NodeID tapNode;

        // TL6-9: the transport's rate/tempo and the engine's round-trip latency, frozen at the
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

    // The master tap, found or created: a "Rec Tap" node spliced IN FRONT OF the Audio Output node,
    // with every connection that fed the output re-routed through it. One compound undo step when
    // it has to be created; a plain lookup (no undo step) when one is already there. Returns
    // nullptr if the patch has no Audio Output node to splice in front of.
    juce::AudioProcessorGraph::Node* ensureMasterRecordTap();

    // The live master tap, or nullptr. Resolves the take's NodeID first and falls back to a scan —
    // an undo taken mid-take rebuilds the graph and renumbers nodes, and losing the tap that way
    // must lose the take, not crash.
    RecordTapModule* findMasterRecordTap() const;

    // Where this take's files go: `<bundle>/Audio/take-N.wav` + `<bundle>/Peaks/take-N.agpk` for a
    // saved project, `<app data>/Recordings/take-N.wav` + `.../Recordings/take-N.agpk` for one that
    // has never been saved (see the unsaved-project policy on ProjectBundle). N is the first free
    // number in whichever folder. Fills the file/assetRef fields of `take`; returns false if the
    // directories could not be created.
    bool chooseTakeFiles(AudioTake& take) const;

    // TL6-3's counterpart to commitMidiRecording(): the ONE place an audio take ever commits. Stops
    // the tap, then creates the clip in a single recordTimelineChange. A no-op unless a take is
    // actually in flight, so both callers (the Record-off click and the poll's commit-on-stop) can
    // call it unconditionally.
    void commitAudioRecording();

    // RAII suspension of automation capture for the duration of a programmatic rewrite. Compiles to
    // an empty object in a SYNTH_ENABLE_TIMELINE=OFF build, so call sites stay #if-free.
    struct ProgrammaticApplyScope {
        explicit ProgrammaticApplyScope(MainComponent& owner)
#if SYNTH_ENABLE_TIMELINE
            : guard(owner.automationRecorder)
#endif
        {
            juce::ignoreUnused(owner);
        }
#if SYNTH_ENABLE_TIMELINE
        synth::AutomationRecorder::ScopedProgrammaticApply guard;
#endif
    };

    // ---- TrackHeaderHost (TL5-3) ----
    std::vector<BindingOption> getAvailableTrackInNodes(synth::TrackId forTrack) override;
    juce::String getNodeDisplayName(const juce::String& uuid) override;
    void bindTrackTo(synth::TrackId track, const juce::String& uuid) override;
    void createAndBindTrackInNode(synth::TrackId track) override;
    void selectNodeInGraph(const juce::String& uuid) override;
    void deleteTrack(synth::TrackId track) override;
    void performTrackEdit(const std::function<void()>& mutation) override;
    void addMidiTrack() override;
    void addAudioTrack() override;

    // Creates a "Track In" node with a fresh uuid at the canvas' left edge, wires it to the single
    // MIDI instrument in the patch when there is exactly one, and returns its uuid (empty on
    // failure). Called INSIDE the caller's undo transaction — it opens none of its own.
    juce::String createTrackInNode();

    // TL6-4's twin of createTrackInNode(): a "Track Audio" node with a fresh uuid, wired stereo into
    // the master bus — the Rec Tap when one is spliced in, otherwise the Audio Output node directly,
    // so the two orderings compose (adding an audio track before or after the first take both end up
    // with the track's audio flowing THROUGH the tap). Returns its uuid, empty on failure. Called
    // INSIDE the caller's undo transaction.
    juce::String createTrackAudioNode();

    // TL6-4. Points the engine's AudioClipStreamer at the current document's asset roots: the open
    // bundle directory (invalid when the project has never been saved) plus the app-data Recordings
    // folder TL6-3 writes unsaved-project takes into. Called wherever `currentBundleDir_` changes.
    void refreshAssetRoots();

    // ---- TL6-6: asset management (import/relink/collect-clean/adopt-on-save) ----

    // Production entry point for the clip-lane area's "Relink audio…" menu item: opens an async
    // FileChooser and, on a choice, calls relinkClipAsset(id, file). A no-op if `id` no longer
    // resolves to a clip by the time the dialog returns.
    void promptRelinkClipAsset(synth::ClipId id);
    // The actual relink: imports `chosenFile` (via synth::AssetManager::importAudioFile into the
    // current bundle, or into the app-data Recordings/ convention when the project has never been
    // saved) and rewrites assetRef on `id` AND every other clip that shared its OLD ref, as ONE
    // undo step (a single AppUndoManager::recordTimelineChange batching every
    // TimelineDoc::setClipAsset call, each preserving its own clip's sourceStartSeconds). Never
    // deletes the old file. A no-op (with a status-bar message) if `id` doesn't resolve, the asset
    // has no ref to relink, or the import fails.
    void relinkClipAsset(synth::ClipId id, const juce::File& chosenFile);

    // synth::AssetManager::cleanUnusedAssets against the current bundle + live timeline doc. 0
    // outside a saved bundle (nothing to sweep). See cleanUnusedAssetsForTest()'s comment for why
    // this has no menu/shortcut wiring yet.
    int cleanUnusedAssets();

    // The graph node carrying this uuid, or nullptr.
    juce::AudioProcessorGraph::Node* findNodeByUuid(const juce::String& uuid) const;

    // ---- File handlers, minus the dialogs ----
    // `file` is whatever the chooser returned; the .agsproj branch is what makes a bundle a bundle.
    void saveToFile(const juce::File& file);
    bool openFromFile(const juce::File& file);
    // One choke point for "load factory preset N + keep the timeline in step", shared by the Load
    // menu and simulateLoadFactoryPresetForTest.
    void loadFactoryPresetAtIndex(int index);
    // New Patch empties the timeline as well as the canvas, as its own undoable step.
    void clearTimelineForNewPatch();

    // ChangeListener (juce::ChangeListener override) — called when ThemeManager broadcasts.
    // Implements the 3-step re-skin pass: applyTheme → sendLookAndFeelChangeMessage → repaint.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Shared initialisation body called from both constructors after appProperties is set up.
    void initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry);

    // Push the (themed, re-tinted) icon Drawables onto the 9 toolbar DrawableButtons + the
    // status-bar master-mute button, and manage icon-only vs icon+text text per narrow mode.
    // dynamic_casts the LnF and no-ops the icon assignment when null (headless tests).
    void applyToolbarIcons();

    // Collapse/expand the library sidebar. Animates to the target layout.
    void setLibraryVisible(bool v);

    // Compute the target bounds for the library and AI panel in the current layout.
    // Pure geometry — no side effects; used by animation and tests.
    struct PanelBoundsResult {
        juce::Rectangle<int> libraryBounds;  // empty rect when hidden
        juce::Rectangle<int> aiPanelBounds;  // empty rect when hidden
        juce::Rectangle<int> timelineBounds; // empty rect when hidden (TL5-1)
        juce::Rectangle<int> graphEditorBounds;
    };
    // timelineVisible is always accepted (even in a SYNTH_ENABLE_TIMELINE=OFF build, callers pass
    // the always-false isTimelineVisible member) so every call site has one uniform signature;
    // the carve driven by it is what's actually gated, inside the .cpp.
    PanelBoundsResult computePanelBounds(bool libVisible, bool aiVisible, bool timelineVisible) const;

    // Update the displayed patch name (status bar). Immediate repaint, no timer delay.
    void setCurrentPatchName(const juce::String& name);

    // Owned fallback objects used when the delegating ctor is called (tests/legacy).
    // Null when the primary ctor is used (refs point at external objects instead).
    std::unique_ptr<synth::theme::ThemeManager> ownedThemeManager;
    std::unique_ptr<synth::theme::AppLookAndFeel> ownedLookAndFeel;

    // Non-owning references to the active ThemeManager and LookAndFeel.
    // Always valid — set by both constructors (either to external objects or to the
    // owned fallbacks above).
    synth::theme::ThemeManager* themeManager{nullptr};
    synth::theme::AppLookAndFeel* lookAndFeel{nullptr};

    // TL5-3: the app's ONE live timeline document, and the recorder that captures parameter
    // gestures into its automation lanes.
    //
    // DECLARATION ORDER IS LOAD-BEARING — both are declared before `undoManager`, so both outlive
    // it: a TimelineSnapshotAction sitting on the undo stack holds a reference to this doc (see
    // AppUndoManager::recordTimelineChange), and members are destroyed in reverse declaration
    // order. The recorder likewise must not be destroyed while an undo action could still commit
    // into it. Both members exist in a SYNTH_ENABLE_TIMELINE=OFF build too (inert: nothing ever
    // mutates the doc, and the recorder is never attached) — only the wiring is gated.
    synth::TimelineDoc timelineDoc;
    synth::AutomationRecorder automationRecorder;
    // TL5-5: the app's one live MidiRecorder — no lifetime constraint against undoManager the way
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
    juce::DrawableButton undoButton{"undo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton redoButton{"redo", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleAiPanelButton{"toggleAi", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleModMatrixButton{"toggleMatrix", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleMinimapButton{"toggleMinimap", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton autoArrangeButton{"autoArrange", juce::DrawableButton::ImageAboveTextLabel};
    juce::DrawableButton toggleLibraryButton{"toggleLibrary", juce::DrawableButton::ImageAboveTextLabel};
#if SYNTH_ENABLE_TIMELINE
    // TL5-1: timeline panel toggle. Gated so a -DSYNTH_ENABLE_TIMELINE=OFF build has no button,
    // no toolbar slot wiring, and no command — see ToolbarComponent::Slot::ToggleTimeline.
    juce::DrawableButton toggleTimelineButton{"toggleTimeline", juce::DrawableButton::ImageAboveTextLabel};
#endif
    juce::DrawableButton themeToggleButton{"toggleTheme", juce::DrawableButton::ImageAboveTextLabel};

    std::unique_ptr<juce::FileChooser> fileChooser;

    synth::AIIntegrationService aiService;
    // Declared BEFORE aiChatComponent: members are destroyed in reverse declaration order, so
    // aiChatComponent (which installs AccountService::onStateChanged/onAccessTokenChanged in
    // setAccountService(), see its header comment) is torn down first, while accountService is
    // still alive to have those callback slots cleared.
    // P4-6: explicit production host — the AccountService(host) default of localhost:8787 is a
    // dev/test convenience only, and MainComponent is the real composition root.
    synth::AccountService accountService{synth::branding::kApiBaseUrl};
    synth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;
    bool isLibraryVisible{true};
    bool isAlignmentGuidesEnabled{true}; // NEW: default TRUE for backward compatibility

    // TL5-1: bottom-docked timeline panel shell. The member exists unconditionally (harmless
    // when SYNTH_ENABLE_TIMELINE is OFF — never made visible, never carved into the layout);
    // only the toolbar button/command/carve that could ever flip isTimelineVisible are gated.
    synth::ui::TimelinePanelComponent timelinePanel;
    bool isTimelineVisible = false;
    // TL5-5: playing->stopped edge detection for the MIDI recorder's auto-commit-on-stop, updated
    // once per 10 Hz poll tick — mirrors AutomationRecorder's own `lastPlaying` bookkeeping.
    bool wasTransportPlaying_ = false;

    // TL6-7: the feedback-guard re-arm latch. True from a guard trip until the explicit reset
    // gesture — the armed-Audio-track set going from NONE armed to at least one armed again (disarm
    // then re-arm). While true, the poll keeps input monitoring off even though an Audio track is
    // still armed; simply staying armed must not re-enable it. Exists unconditionally (inert with
    // the flag off, like every member in this block); only the poll that writes it is
    // #if SYNTH_ENABLE_TIMELINE.
    bool feedbackGuardLatched_ = false;
    // Previous poll's "is any Audio-kind track armed" result — the FALSE -> TRUE edge is what
    // clears the latch above.
    bool wasAnyAudioTrackArmed_ = false;

    // TL6-3: the in-flight audio take (see the AudioTake declaration above).
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

    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    // TL5-10: consulted first by resolveEditSurface(); std::nullopt means "use real focus".
    std::optional<EditSurface> editSurfaceOverrideForTest_;

#if JUCE_MAC
    synth::update::UpdateManager updateManager;
#endif

    // ---- Panel slide animations (time-bounded, auto-stop) ----
    // One VBlankAnimatorUpdater shared by both panel animations (driven by MainComponent).
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    synth::ui::AnimationDriver libraryAnim;
    synth::ui::AnimationDriver aiPanelAnim;

    // During animation, track the "from" bounds so lerpBounds() can interpolate.
    juce::Rectangle<int> libraryAnimFrom;
    juce::Rectangle<int> aiPanelAnimFrom;
    juce::Rectangle<int> timelineAnimFrom; // TL5-1; unused (always empty) when the flag is OFF
    juce::Rectangle<int> graphEditorAnimFrom;

    // Start a coordinated bounds animation for library + AI panel + timeline panel + graph editor.
    // fromResult is the current layout; toResult is the target layout.
    void animatePanelTransition(const PanelBoundsResult& fromResult, const PanelBoundsResult& toResult,
                                bool hideLibraryOnComplete, bool hideAiPanelOnComplete, bool hideTimelineOnComplete);

    // Alignment guides toggle (UI Phase 7 - Item 4)
    void setAlignmentGuidesEnabled(bool enabled);

    // Provides native-style tooltips for any child Component that has a tooltip
    // string set via setTooltip(). Constructed last so all child components exist.
    // Do NOT set tooltips on controls here; each feature owner does that.
    juce::TooltipWindow tooltipWindow{this};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
