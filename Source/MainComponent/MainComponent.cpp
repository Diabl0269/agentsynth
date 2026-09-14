#include "MainComponent.h"
#include "AI/AIProviderRegistry.h"
#include "Branding.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ProjectBundle.h"
#include "Timeline/AssetManager.h"
#include "UI/Settings/PreferencesSettingsTab.h"
#include "UI/Settings/SettingsWindow.h"
// Generated at CMake CONFIGURE time from local git history — see the root CMakeLists.txt's
// "What's New" block. ${CMAKE_BINARY_DIR}/generated is on AppUI's private include path.
#include "WhatsNewData.h"
#include <algorithm>

namespace {

// A clip's assetRef always names the .wav asset — chooseTakeFiles() is what establishes the
// pairing with its .agpk peaks sidecar: same stem, and either a sibling "Peaks/" directory (a saved
// bundle: "Audio/take-n.wav" <-> "Peaks/take-n.agpk") or the SAME "Recordings/" directory (an
// unsaved project, where chooseTakeFiles points audioDir and peaksDir at the same root). Returns
// the peaks SIDECAR'S ref, in the same bundle/root-relative form the streamer's own
// resolveAssetRef() understands — so that one function stays the single place a ref becomes a
// juce::File, for both the audio and the peaks half. Empty in, empty out.
juce::String peaksRefForAssetRef(const juce::String& assetRef) {
    if (assetRef.isEmpty())
        return {};

    juce::String ref = assetRef;
    const juce::String audioPrefix = juce::String(synth::ProjectBundle::kAudioSubdirName) + "/";
    if (ref.startsWith(audioPrefix))
        ref = juce::String(synth::ProjectBundle::kPeaksSubdirName) + "/" + ref.substring(audioPrefix.length());

    return ref.upToLastOccurrenceOf(".", false, false) + ".agpk";
}

} // namespace

// ---- Primary constructor (injected ThemeManager + LookAndFeel from Main.cpp) ----
MainComponent::MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                             std::unique_ptr<synth::AIProvider> provider)
    : ownedAudioEngine(std::make_unique<AudioEngine>(AudioEngine::HostMode::Standalone))
    , audioEngine(*ownedAudioEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    // Setup ApplicationProperties — the shared location, never a local copy of the fields (the
    // plugin processor opens the same file for the scan list; see synth::userSettingsOptions()).
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Restore persisted theme and apply it. Must come AFTER appProperties is configured.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());

    // Subscribe to theme changes so we can re-skin on every switch.
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), synth::AIProviderRegistry::createDefault());
}

// ---- Plugin constructor (engine owned by AgentSynthAudioProcessor) ----
// Identical to the primary ctor apart from where the engine comes from; the shared body lives in
// initialiseCommon(), which skips engine initialise/shutdown when we don't own the engine.
MainComponent::MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                             AudioEngine& externalEngine, std::unique_ptr<synth::AIProvider> provider)
    : audioEngine(externalEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), synth::AIProviderRegistry::createDefault());
}

// ---- Delegating constructor for tests / legacy call sites ----
MainComponent::MainComponent(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry)
    : ownedAudioEngine(std::make_unique<AudioEngine>(AudioEngine::HostMode::Standalone))
    , audioEngine(*ownedAudioEngine)
    , graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties) {
    // Own a default ThemeManager + LookAndFeel so the code behaves identically
    // to the primary-ctor path (no special-casing in the rest of the class).
    ownedThemeManager = std::make_unique<synth::theme::ThemeManager>();
    ownedLookAndFeel = std::make_unique<synth::theme::AppLookAndFeel>();
    themeManager = ownedThemeManager.get();
    lookAndFeel = ownedLookAndFeel.get();

    // Setup ApplicationProperties (same as primary ctor)
    propertiesOptions = synth::userSettingsOptions();
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Initialise theme with appProperties so the persisted theme is restored.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), std::move(registry));
}

// ---- Shared post-construction body ----
void MainComponent::initialiseCommon(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry) {
    // Route AI patch applies through the app undo manager so Apply/Merge on a patch card is Cmd+Z-able.
    // Safe in both ctors: undoManager is declared before aiService, so it is already constructed here.
    aiService.setUndoManager(&undoManager);

    // juce::UndoManager is a ChangeBroadcaster that fires on every perform/undo/redo — the one
    // signal that means "something changed since the last save/load" without this class having to
    // hook every individual mutation site. changeListenerCallback dispatches on the source, so this
    // never fires the theme re-skin or settings-file branches.
    undoManager.getUndoManager().addChangeListener(this);

    // ORDERING CONTRACT: read the persisted panel-visibility flags FIRST, before any
    // setVisible()/addAndMakeVisible() call that depends on them. These override the member
    // initialisers (isLibraryVisible{true}, isAiPanelVisible=false).
    isLibraryVisible = appProperties.getUserSettings()->getBoolValue("librarySidebarVisible", true);
    isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
    isTimelineVisible = appProperties.getUserSettings()->getBoolValue("timelinePanelVisible", false);
    // ...and snap the fractions resized() lays the panels out from onto them. A restore must never
    // itself look like a panel sliding open, and the first resized() (setSize() at the end of this
    // function) runs before any window exists — see beginPanelSlide().
    librarySlide_.snapTo(isLibraryVisible ? 1.0f : 0.0f);
    aiPanelSlide_.snapTo(isAiPanelVisible ? 1.0f : 0.0f);
    timelineSlide_.snapTo(isTimelineVisible ? 1.0f : 0.0f);
    // The theme metric is the DEFAULT height, not the law: a height the user dragged wins. Clamped
    // here and on every resized() — see clampTimelinePanelHeight().
    timelinePanelHeight_ = clampTimelinePanelHeight(
        appProperties.getUserSettings()->getIntValue(kTimelinePanelHeightKey, defaultTimelinePanelHeight()));
    graphEditor.setAlignmentGuidesEnabled(
        appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true));
    graphEditor.setSmartConnectionMode(GraphEditor::smartConnectionModeFromString(
        appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired")));
    graphEditor.setDoubleClickPortDisconnectEnabled(
        appProperties.getUserSettings()->getBoolValue("doubleClickPortDisconnect", true));
    // T148 (docs/macros_implementation.md §7 item 9): both default ON — see PreferencesSettingsTab's own toggle
    // comments for why these are plain on/off rather than the tri-state macroAutoPortPreference.
    graphEditor.setAutoCreateMacroPortsOnDragEnabled(
        appProperties.getUserSettings()->getBoolValue("macroAutoCreatePortsOnDrag", true));
    graphEditor.setAutoDeleteMacroPortsOnLastCableEnabled(
        appProperties.getUserSettings()->getBoolValue("macroAutoDeletePortsOnLastCable", true));
    // T184 (P9-3c, docs/mixer.md §5.2 "main workflow"): default ON — see PreferencesSettingsTab's
    // own toggle comment for why this is a plain on/off rather than a tri-state preference.
    graphEditor.setAutoCreateChannelOnConnectEnabled(
        appProperties.getUserSettings()->getBoolValue("mixerAutoCreateChannelOnConnect", true));
    // Stored here, but APPLIED to the patch further down — the default preset does not exist yet.
    // AudioEngine::initialise() builds it, and that runs at the end of this constructor. See
    // applyStoredDualIOPreferenceToPatch().
    graphEditor.setDefaultDualIOForNewModules(
        appProperties.getUserSettings()->getBoolValue("defaultDualIOForNewModules", false));
    // Per-module overrides of the default above (Preferences → "Per-module I/O defaults..."),
    // same new-modules-only scope as the toggle just above — no patch to retro-apply here either.
    graphEditor.setDualIOPerModuleOverrides(PreferencesSettingsTab::loadDualIOPerModuleOverrides(appProperties));
    // Macro auto-port boundary tri-state (founder-review F5): restored the SAME way as the two
    // on/off macro automations and the Dual I/O overrides above, so a "Remember my choice" from the
    // auto-port modal the previous session survives a relaunch. Without this the editor's tri-state
    // is only ever pushed when the Settings window opens (PreferencesSettingsTab::setGraphEditor),
    // so a fresh launch left it Unset and the modal re-asked every session (T147).
    graphEditor.setMacroAutoPortPreference(PreferencesSettingsTab::loadMacroAutoPortPreference(appProperties));

    // Minimap overlay visibility (issue #159), defaults to visible.
    const bool minimapVisible = appProperties.getUserSettings()->getBoolValue("minimapVisible", true);
    graphEditor.setMinimapVisible(minimapVisible);

    // Scroll direction, and the LIVE path for it. juce::PropertiesFile is a ChangeBroadcaster that
    // fires on every value written, so subscribing here is what lets a Preferences toggle reach the
    // timeline and the piano roll without a restart — and without SettingsWindow having to grow yet
    // another constructor callback to hand down (the "Show timeline" kill switch already needs one,
    // and one wire per preference does not scale). changeListenerCallback dispatches on the source,
    // so a settings write never triggers the theme re-skin and vice versa.
    if (auto* settings = appProperties.getUserSettings())
        settings->addChangeListener(this);
    applyNaturalScrollingPreference();
    applyZoomScrollPreference();

    // Cable colour config (issue #157). Restored HERE rather than only in AppearanceSettingsTab:
    // that tab is built lazily when the Settings window opens, so leaving it to the tab would
    // mean the canvas ignored the user's saved colours until they went looking for them.
    graphEditor.setCableColourMode(synth::ui::loadCableColourMode(*appProperties.getUserSettings()));
    graphEditor.setCableColourOverrides(synth::ui::loadCableColourOverrides(*appProperties.getUserSettings()));

    // Macro recolour picker's favourites shelf (P8-14): the same PropertiesFile the timeline
    // ruler's marker colour picker persists to (TimelinePanelComponent::setApplicationProperties
    // -> ruler_.setPropertiesFile), so a favourite saved from one is offered by the other.
    graphEditor.setPropertiesFile(appProperties.getUserSettings());

    // Wavetable browser folder (issue #180): GraphEditor holds the value so every Wavetable
    // card can seed its browser from it, MainComponent owns the ApplicationProperties round
    // trip — the same split as the cable-colour config above.
    {
        const juce::String saved = appProperties.getUserSettings()->getValue("wavetableFolder", juce::String());
        if (saved.isNotEmpty())
            graphEditor.rememberWavetableFolder(juce::File(saved));
    }
    graphEditor.onWavetableFolderChanged = [this](const juce::File& folder) {
        appProperties.getUserSettings()->setValue("wavetableFolder", folder.getFullPathName());
        appProperties.saveIfNeeded();
    };

    if (provider) {
        aiService.setProvider(std::move(provider));
    } else {
        // Load AI provider preference. The persisted id (see AIProviderRegistry) is not a
        // display name — registry.create() falls back to the first registered provider ("ollama")
        // if the saved id is unknown (e.g. stale pre-registry value, or empty).
        //
        // P4-6 migration: "aiProvider" is only ever WRITTEN by AISettingsTab::updateSettings(), so
        // most existing installs have never persisted it, even after months of use — its absence
        // alone can't distinguish "brand new install" from "existing user who never opened AI
        // settings". existsAsFile() can: it reflects whether the settings file was already on disk
        // before this launch touched anything (nothing above this point in initialiseCommon(), nor
        // shortcutManager.loadFromProperties()/themeManager->initialise() in the constructor, writes
        // to appProperties — all read-only). See resolveDefaultProviderId() for the pure decision.
        const bool hasExistingSettingsFile = appProperties.getUserSettings()->getFile().existsAsFile();
        const juce::String defaultProviderId = resolveDefaultProviderId(hasExistingSettingsFile);
        juce::String savedProviderId = appProperties.getUserSettings()->getValue("aiProvider", defaultProviderId);

        // Pin the resolved id so every other reader of "aiProvider" (AISettingsTab) agrees with
        // what actually got constructed here, instead of independently re-deriving a default.
        // saveIfNeeded() is required, not optional: without it, a fresh install that resolves to
        // "remote" here only holds that in memory — if the process exits before some OTHER write
        // flushes the file, launch 2 finds a settings file on disk (from this launch's theme/
        // shortcut/panel-visibility writes) with no "aiProvider" key in it, resolves
        // hasExistingSettingsFile=true, and silently reverts a brand new install to "ollama".
        if (!appProperties.getUserSettings()->containsKey("aiProvider")) {
            appProperties.getUserSettings()->setValue("aiProvider", savedProviderId);
            appProperties.saveIfNeeded();
        }

        // Each provider persists its own host under its own key — "ollamaHost" and "remoteHost"
        // must never collide, or switching providers in Settings silently points one of them at
        // the other's address (see AISettingsTab::hostSettingsKeyFor()). An empty remoteHost
        // default lets AIProviderRegistry::createDefault() fall back to
        // synth::branding::kApiBaseUrl.
        const juce::String hostKey = savedProviderId == "remote" ? "remoteHost" : "ollamaHost";
        const juce::String hostDefault =
            savedProviderId == "remote" ? juce::String() : juce::String("http://localhost:11434");
        juce::String savedHost = appProperties.getUserSettings()->getValue(hostKey, hostDefault);

        aiService.setProvider(registry.create(savedProviderId, {savedHost, {}}));
    }

    // ORDERING CONTRACT: aiChatComponent is a member, so its constructor (which calls
    // refreshModels()) already ran BEFORE this body — at that point aiService had no
    // provider, so discovery short-circuited and no model was ever selected. We must
    // refresh again HERE, after setProvider(), or currentModel stays empty and every
    // /api/chat request is rejected by Ollama with HTTP 400 "model is required".
    // Regression: see #96 / f7cba4a.
    aiChatComponent.refreshModels();

    // Same ORDERING CONTRACT as refreshModels() above: aiChatComponent's constructor read
    // "aiRequestTimeoutMs" before appProperties.setStorageParameters() (called earlier in this
    // body) had pointed it at the real settings file, so that read saw an empty store and fell
    // back to the default. Re-load and re-push now that the file is actually open.
    aiChatComponent.setRequestTimeoutMs(appProperties.getUserSettings()->getIntValue(
        "aiRequestTimeoutMs", synth::AIChatComponent::kDefaultRequestTimeoutMs));

    // Gives AIIntegrationService's outgoing-request context builder a way to read the
    // app's one live TimelineDoc/TransportService — see AIIntegrationService::setTimelineContext().
    // Both outlive aiService (declaration order: timelineDoc, then audioEngine's referent, then
    // aiService), so this pointer never dangles for aiService's lifetime.
    aiService.setTimelineContext(&timelineDoc, &audioEngine.getTransport());
    // The timeline is GA: the AI's timeline/automation authoring surface is on unconditionally
    // from first launch (no Preferences toggle left to react to).
    aiService.setTimelineToolsEnabled(true);
    // The chat's Patch/Arrange selector reads this switch but gets no notification of it — the
    // refreshModels() call above ran BEFORE the switch (and before the timeline context existed),
    // so its gate check saw "off". Re-sync now that both are installed; same ownership shape as
    // the refreshModels() ordering contract itself.
    aiChatComponent.refreshModeControls();

    // The WRITE half. The service only ever holds the doc as const (it is a context reader),
    // and it owns no undo manager for the timeline, so the host supplies the apply path — the same
    // objects every other timeline edit in this class goes through, which is what puts an AI-applied
    // batch on the one shared undo stack alongside the user's own edits. `this` is safe to capture:
    // aiService is a member destroyed with us, and it clears the callback with it.
    aiService.setTimelineOpsApplyCallback([this](const juce::var& envelope) {
        return synth::TimelineOps::apply(envelope, timelineDoc, audioEngine.getGraph(), undoManager);
    });

    // Wire the account row/dialog up BEFORE attemptSilentSignIn() so the wiring is live for any
    // state changes that arrive from it (P3-2: sign-in surface for the AI panel).
    aiChatComponent.setAccountService(&accountService);
    accountService.attemptSilentSignIn();

    aiService.addListener(this);
    undoManager.setGraphEditor(&graphEditor);
    setWantsKeyboardFocus(true);

    // ---- Snippets + library collapse state (issue #156) ----
    // GraphEditor owns no file dialogs and the sidebar owns no filesystem access, so
    // MainComponent brokers between them.
    graphEditor.onSaveSnippetRequested = [this] { promptSaveSnippet(); };
    // Macros (P8-12): GraphEditor owns no status bar — see onStatusMessage's own comment.
    graphEditor.onStatusMessage = [this](const juce::String& msg) { statusBar.showMessage(msg); };
    // FRO25 (P9-3d): the canvas/module menus' "Make Channel" and "Duplicate into Channel" route
    // here so their ONE undo step also covers the timeline and runs the reconcile pass.
    graphEditor.onMakeChannelRequested = [this](juce::AudioProcessorGraph::NodeID source) {
        makeChannelForNode(source);
    };
    graphEditor.onDuplicateIntoChannelRequested = [this](juce::AudioProcessorGraph::NodeID nodeId,
                                                         const juce::String& macroId) {
        duplicateIntoChannel(nodeId, macroId);
    };
    // Right-click-any-knob -> the automation lane editor. Mirrors onSaveSnippetRequested's
    // shape exactly — GraphEditor owns no TimelineDoc, so it hands the (nodeId, paramId) pair back
    // to the one component that owns both the doc and the graph.
    graphEditor.onAutomateParameterRequested = [this](juce::AudioProcessorGraph::NodeID nodeId,
                                                      const juce::String& paramId) {
        automateParameter(nodeId, paramId);
    };
    // A hosted-plugin card's "Open Editor" button. Mirrors onAutomateParameterRequested's
    // shape — GraphEditor owns neither the module lookup nor the window manager.
    graphEditor.onOpenPluginEditorRequested = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        if (auto* node = audioEngine.getGraph().getNodeForId(nodeId))
            if (auto* hostedPlugin = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor()))
                pluginWindowManager.openEditorFor(hostedPlugin, nodeId);
    };
    graphEditor.snippetProvider = [this](const juce::String& name) -> juce::var {
        return synth::SnippetManager::loadSnippet(
            synth::SnippetManager::fileForName(synth::SnippetManager::getDefaultSnippetsDirectory(), name));
    };
    moduleLibrary.onSnippetDeleteRequested = [this](const juce::String& name) {
        if (synth::SnippetManager::deleteSnippet(synth::SnippetManager::getDefaultSnippetsDirectory(), name)) {
            refreshSnippetLibrary();
            statusBar.showMessage("Deleted snippet \"" + name + "\"");
        }
    };
    moduleLibrary.onCollapseStateChanged = [this] {
        appProperties.getUserSettings()->setValue("libraryCollapsedSections",
                                                  moduleLibrary.getCollapsedSections().joinIntoString("\n"));
        appProperties.saveIfNeeded();
    };
    moduleLibrary.setCollapsedSections(juce::StringArray::fromLines(
        appProperties.getUserSettings()->getValue("libraryCollapsedSections", juce::String())));
    refreshSnippetLibrary();

    // ---- Hosted plugins -----------------------------------------------------------
    // Restore the saved scan list, install it as the process-wide identity resolver, and wire the
    // two sidebar callbacks. Nothing here starts a scan: scanning launches child processes and is
    // only ever done because the user asked (and never at all in a hosted build — see
    // startPluginScan). The list IS needed in a hosted build, because a DAW session that hosts a
    // plugin still has to resolve its identity to something.
    auto* pluginBackend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault());
    if (ownedAudioEngine == nullptr && pluginBackend != nullptr && pluginBackend->getScanService() != nullptr) {
        // The plugin path with a resolver already installed: it is AgentSynthAudioProcessor's, and
        // it outlives this editor (a host closes and reopens the window freely). Adopt it instead of
        // installing ours over it — replacing it would leave the session with a resolver that dies
        // with the window, which is the bug this branch exists to prevent.
        activeScanService = pluginBackend->getScanService();
    } else {
        if (auto savedScanList = juce::parseXML(appProperties.getUserSettings()->getValue(kPluginScanListKey)))
            pluginScanService.loadFromXml(*savedScanList);
        if (pluginBackend != nullptr)
            pluginBackend->setScanService(&pluginScanService);
    }
    if (auto savedRecentProjects = juce::parseXML(appProperties.getUserSettings()->getValue(kRecentProjectsKey)))
        recentProjects.loadFromXml(*savedRecentProjects);

    // FRO44: whichever scan service ended up active above (ours, or the plugin path's adopted one),
    // register on it so pluginScanCompleted() fires for every real scan — the manual "Scan for
    // plugins..." row below, and maybeStartEagerPluginScan() (called by Main.cpp, never from here).
    getPluginScanService().addListener(this);

    moduleLibrary.onScanPluginsRequested = [this] { startPluginScan(); };
    moduleLibrary.onPluginActivated = [this](const synth::PluginIdentity& identity) {
        graphEditor.addHostedPluginAtCanvasPosition(identity, graphEditor.getViewportCentreInCanvasSpace());
    };
    // T160: Enter-to-insert on a keyboard-focused Module/Snippet row — the click-to-add path those
    // two row kinds never had before (mouseDown starts a drag for them immediately; see
    // ModuleLibraryComponent's own comment on onModuleActivated/onSnippetActivated). Both land at
    // the viewport centre, mirroring onPluginActivated above and the "drop with no cursor position"
    // fallback GraphEditor::itemDropped already uses.
    moduleLibrary.onModuleActivated = [this](const juce::String& name) {
        graphEditor.addModuleAtCanvasPosition(name, graphEditor.getViewportCentreInCanvasSpace(), {});
    };
    moduleLibrary.onSnippetActivated = [this](const juce::String& name) {
        if (!graphEditor.snippetProvider)
            return;
        auto snippet = graphEditor.snippetProvider(name);
        if (!snippet.isObject())
            return;
        graphEditor.insertSnippetAt(snippet, graphEditor.getViewportCentreInCanvasSpace());
    };
    refreshPluginLibrary();
    // Register commands for the macOS native menu bar (Edit→Undo shows Cmd+Z).
    // Do NOT add commandManager.getKeyMappings() as a KeyListener — it intercepts
    // keys like Cmd+Shift+Z and silently fails to invoke the command, preventing
    // our keyPressed() fallback from running. All key dispatch goes through keyPressed().
    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    shortcutManager.onBindingsChanged = [this] { updateCommandShortcuts(); };
    startTimerHz(10);
    addAndMakeVisible(graphEditor);
    addAndMakeVisible(moduleLibrary);

    // Grey out the singleton I/O rows once the patch already has one, and repaint the library
    // whenever the graph's module set changes so that state stays accurate.
    moduleLibrary.isModuleAvailable = [this](const juce::String& name) {
        return !GraphEditor::isSingletonIOModule(name) ||
               !GraphEditor::graphHasModuleNamed(audioEngine.getGraph(), name);
    };
    graphEditor.onGraphStructureChanged = [this] {
        moduleLibrary.repaint();
        // Close-on-node-delete. A pure NodeID -> graph lookup — see
        // HostedPluginWindowManager::pruneClosedNodes for why it must never dereference the module a
        // removed node used to carry.
        pluginWindowManager.pruneClosedNodes(audioEngine.getGraph());
        // The same catch-all, for the other hosted-plugin observer pair. A node that has just
        // appeared (a library drop, a preset load, an undo restore) needs MainComponent's latency /
        // publish callbacks installed on it, and this is the one hook every path that adds a node
        // already runs through. Idempotent — re-assigning the same two slots costs nothing.
        installHostedPluginObservers();
        // Safety net for graph changes with no explicit post-apply site of their own — the
        // canonical one being "the user deleted the Track In node from the canvas", which goes
        // through recordStructuralChange (a RECORD, not a restore, so the undo hooks don't fire).
        //
        // Deliberately reconcile-ONLY, never an unconditional republish: this runs at the end of
        // every updateComponents(), and building a snapshot each time would be wasteful. A
        // reconcile that flips a flag is itself a doc mutation, so timelineChanged republishes for
        // exactly the cases that need it — a binding can only start or stop resolving when a node
        // appears or disappears, which is also the only way an orphan flag moves.
        reconcileTimelineBindingsOnly();
    };
    addAndMakeVisible(aiChatComponent);
    aiChatComponent.setVisible(isAiPanelVisible);
    moduleLibrary.setVisible(isLibraryVisible);
    // Added unconditionally — isTimelineVisible stays false forever in a flag-OFF build
    // (the only code that ever flips it, the toggle button's onClick, is gated below), so this is
    // an inert invisible child there, same as any other never-shown component.
    addAndMakeVisible(timelinePanel);
    timelinePanel.setVisible(isTimelineVisible);
    graphEditor.getModMatrix().setVisible(graphEditor.isModMatrixVisible());

    // Z-ORDER CONSTRAINT: add the toolbar strip + status bar BEFORE the toolbar buttons.
    // JUCE paints children in addAndMakeVisible order, so the toolbar background must be
    // registered first (the buttons are direct children of MainComponent and paint on top).
    addAndMakeVisible(toolbar);
    addAndMakeVisible(statusBar);

    // Buttons
    addAndMakeVisible(newButton);
    newButton.setComponentID("newButton");
    newButton.onClick = [this] { commandManager.invokeDirectly(AppCommands::newPatch, true); };

    addAndMakeVisible(saveButton);
    saveButton.setComponentID("saveButton");
    saveButton.onClick = [this] { performSaveProject(false); };

    addAndMakeVisible(loadButton);
    loadButton.setComponentID("loadButton");
    loadButton.onClick = [this] {
        juce::PopupMenu menu;
        auto presets = synth::PresetManager::getPresetList();
        auto categories = synth::PresetManager::getCategories();
        // Recent projects, pruned of anything that vanished from disk since the last build (a moved
        // or deleted bundle) — this is also the only time the pruned list needs re-persisting — so
        // it is gathered first regardless of where it is shown.
        if (recentProjects.pruneMissing() > 0)
            saveRecentProjects();
        auto recents = recentProjects.getEntries();

        // P8-31: the flat "Open Project / Open Patch / Recent Projects" layout made a patch and a
        // whole project indistinguishable. Split the menu into two first-level entries: PATCHES holds
        // the default (factory) patches plus "Open a patch…"; PROJECTS holds the recent projects plus
        // "Open a project…". The leaf item ids (preset index, 1000/1001, 2000+) and the callback
        // below are unchanged, so nesting the submenus is purely presentational.
        juce::PopupMenu patchesSubmenu;
        for (const auto& cat : categories) {
            juce::PopupMenu catSubmenu;
            for (int i = 0; i < presets.size(); ++i) {
                if (presets[i].category == cat)
                    catSubmenu.addItem(i + 1, presets[i].name);
            }
            if (catSubmenu.getNumItems() > 0)
                patchesSubmenu.addSubMenu(cat, catSubmenu);
        }
        patchesSubmenu.addSeparator();
        patchesSubmenu.addItem(1001, "Open Patch...");

        juce::PopupMenu projectsSubmenu;
        for (int i = 0; i < (int)recents.size(); ++i)
            projectsSubmenu.addItem(2000 + i, recents[(size_t)i].getFileNameWithoutExtension());
        if (recents.size() > 0)
            projectsSubmenu.addSeparator();
        projectsSubmenu.addItem(1000, "Open Project...");

        menu.addSubMenu("Patches", patchesSubmenu);
        menu.addSubMenu("Projects", projectsSubmenu);

        // Capture `recents` by value — the outer local is gone by the time the async callback
        // runs. `presets` no longer needs capturing here — loadPresetGuarded() below fetches its
        // own copy (same reasoning as launchOpenPresetChooser not needing the menu's own list).
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton), [this, recents](int result) {
            if (result == 1000) {
                openProjectFromFile();
            } else if (result == 1001) {
                openPresetFromFile();
            } else if (result >= 2000) {
                const auto index = (size_t)(result - 2000);
                if (index >= recents.size())
                    return;
                // Same guard as "Open Project...": the recent project itself is opened through
                // openRecentProjectGuarded (shared with the welcome screen's recent-project rows,
                // T114/P8-10), which re-adds it (moving it back to the front) on success.
                openRecentProjectGuarded(recents[index]);
            } else if (result > 0) {
                // Shared with the welcome screen's "Open our default project" button (T114/P8-10)
                // — see loadPresetGuarded.
                loadPresetGuarded(result - 1);
            }
        });
    };

    addAndMakeVisible(undoButton);
    undoButton.setComponentID("undoButton");
    undoButton.setEnabled(false);
    undoButton.onClick = [this] {
        if (undoManager.canUndo())
            undoManager.undo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(redoButton);
    redoButton.setComponentID("redoButton");
    redoButton.setEnabled(false);
    redoButton.onClick = [this] {
        if (undoManager.canRedo())
            undoManager.redo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(toggleAiPanelButton);
    toggleAiPanelButton.setComponentID("toggleAiPanel");
    toggleAiPanelButton.onClick = [this] {
        isAiPanelVisible = !isAiPanelVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("aiPanelVisible", isAiPanelVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        // Everything geometric — showing/hiding the panel, the slide, the synchronous landing when
        // there is no VBlank to slide on — belongs to the one shared seam.
        beginPanelSlide();
    };

    // Bottom-docked timeline panel toggle. Mirrors the AI-panel handler above exactly (flip +
    // persist BEFORE the slide, applyToolbarIcons, then beginPanelSlide) — the axis it slides on
    // is resized()'s business, not the toggle's.
    // Wire the panel to the real transport + persisted settings.
    timelinePanel.setTransport(&audioEngine.getTransport());
    timelinePanel.setMetronome(&audioEngine.getMetronome());
    timelinePanel.setApplicationProperties(&appProperties);

    // The user's bindings for the three surfaces that resolve their OWN keys (see
    // PianoRollComponent::setShortcutManager for the strict-resolution contract). All three are
    // installed together and MUST stay together: with a manager installed, resolution is strict —
    // an action id missing from ShortcutManager::resetToDefaults() has NO key at all rather than
    // falling back to its hardcoded default, so installing the manager before registering an id
    // makes that key silently inert. ShortcutManagerTest's surface-id tripwire pins the id list
    // these three consult against the defaults table for exactly that reason.
    timelinePanel.setShortcutManager(&shortcutManager);
    timelinePanel.getPianoRoll().setShortcutManager(&shortcutManager);
    timelinePanel.getClipLaneArea().setShortcutManager(&shortcutManager);

    // The panel's top-edge drag reports a desired height; THIS component owns it — clamp, lay out
    // live, and persist once the drag ends (not per pixel).
    timelinePanel.onResizeHeight = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight, /*persist=*/false);
    };
    timelinePanel.onResizeHeightCommitted = [this](int desiredHeight) {
        setTimelinePanelHeight(desiredHeight, /*persist=*/true);
    };

    // This component owns the app's one live TimelineDoc, so it owns the four hooks that
    // keep the rest of the system in step with it. The full inventory is in docs/architecture.md
    // ("App wiring") — keep the two in sync.
    //
    //  1. PUBLISH-ON-CHANGE: every effective doc mutation notifies us, and we republish the
    //     snapshot to the audio thread and rebuild the recorder's lane bindings.
    //  2. RECORDER: attached to (doc, undo, transport) and registered with the engine so the
    //     applier can see its gesture claims; driven by update() on the existing 10 Hz timer.
    //  3. RESTORE HOOKS: undo/redo suspends capture for the span of the restore and reconciles
    //     bindings afterwards (both domains — see AppUndoManager::setRestoreHooks).
    //  4. PANEL: the track-header column reads the doc and calls back into us (TrackHeaderHost)
    //     to create, re-bind and delete the Track In nodes its chips name.
    timelineDoc.addListener(this);
    automationRecorder.attachTo(timelineDoc, undoManager, audioEngine.getTransport());
    audioEngine.setAutomationRecorder(&automationRecorder);
    undoManager.setRestoreHooks(
        [this] { programmaticApplyScopes.push_back(std::make_unique<ProgrammaticApplyScope>(*this)); },
        [this] {
            if (!programmaticApplyScopes.empty())
                programmaticApplyScopes.pop_back();
            reconcileTimelineAfterGraphChange();
        });
    timelinePanel.setTrackHeaderHost(this);
    timelinePanel.setTimelineDoc(&timelineDoc);
    // Same undo stack every graph/timeline mutation already shares — a clip drag/trim/
    // split/duplicate/delete is one more AppUndoManager::recordTimelineChange call, same as every
    // other timeline-only edit.
    timelinePanel.setUndoManager(&undoManager);
    // The SAME resolution the audio streamer uses (AudioClipStreamer::resolveAssetRef),
    // re-targeted at the peaks sidecar via peaksRefForAssetRef() — see that function's comment.
    // One shared resolver: the clip-lane area never re-derives bundle-vs-Recordings root logic.
    timelinePanel.getClipLaneArea().setPeaksResolver([this](const juce::String& assetRef) -> juce::File {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(peaksRefForAssetRef(assetRef));
    });
    // Same resolution playback uses, answering existence rather than handing back a File —
    // what paints the missing-asset placeholder instead of an (impossible) waveform.
    timelinePanel.getClipLaneArea().setAssetExistsResolver([this](const juce::String& assetRef) -> bool {
        return audioEngine.getAudioClipStreamer().resolveAssetRef(assetRef) != juce::File();
    });
    // "Relink audio…" bubbles up here rather than being handled inside the lane area itself
    // — it needs a host FileChooser and synth::AssetManager import, neither of which that class has.
    timelinePanel.getClipLaneArea().onRelinkAudioRequested = [this](synth::ClipId id) { promptRelinkClipAsset(id); };
    // Same division of labour for the authoring gestures: the lane area decides WHICH audio track
    // and WHICH beat (double-click on an empty audio row, or an OS file drop on one), and this owns
    // the import + clip creation, because only it knows the bundle root.
    timelinePanel.getClipLaneArea().onAudioFileDropped = [this](synth::TrackId track, double startBeat,
                                                                juce::File file) {
        importAudioFileToClip(track, startBeat, file);
    };
    // P on the clip lanes = loop the selection. The lane area knows the span; only this owns the
    // transport (same division as every other callback above). Whether P also ARMS looping is the
    // "timelineLoopSelectionArms" preference (default yes; off = place the locators, keep the
    // current loop state) — the same key TimelinePanelComponent's own P fallback reads.
    timelinePanel.getClipLaneArea().onLoopRangeRequested = [this](double startBeat, double endBeat) {
        auto& transport = audioEngine.getTransport();
        bool arm = true;
        if (auto* settings = appProperties.getUserSettings())
            arm = settings->getBoolValue("timelineLoopSelectionArms", true);
        transport.setLoop(startBeat, endBeat, arm || transport.getPositionSnapshot().looping);
    };
    // BEFORE the first publish below — publishTimeline() syncs the clip streamer, and it can
    // only resolve an asset ref once it knows the roots.
    refreshAssetRoots();
    // One publish before anything else happens, so the audio thread starts from this document
    // rather than from the exchange's never-published empty fallback.
    publishTimelineAndRebindRecorder();

    // MidiRecorder is now app-wired (docs/architecture.md's hook inventory gains a
    // fifth entry) — this component owns the one live MidiRecorder, since it is the only thing
    // that can see both the armed tracks (timelineDoc) and the transport bar's record button.
    audioEngine.setMidiCaptureSink(&midiRecorder);
    timelinePanel.getTransportBar().onRecordToggled = [this](bool wantRecording) {
        if (!wantRecording) {
            // Both are no-ops unless their own kind of take is in flight, so Record-off can call
            // them unconditionally and neither path has to know the other exists.
            commitAudioRecording();
            commitMidiRecording();
            return;
        }

        // Record does NOT require an armed track (see docs/timeline_panel_core.md's transport
        // section) — pressing Record always rolls the transport with the record indicator lit,
        // capturing on whichever track (if any) happens to be armed. With nothing armed this is
        // identical to Play plus a lit record indicator, plus a transient status-bar notice so the
        // silence isn't mistaken for a bug.
        //
        // The lookup considers Audio tracks too, and FIRST-ARMED WINS. With one armed track
        // of each kind the one earlier in the document decides which kind of take this is; there is
        // deliberately no "record both at once" (two takes, two commits, two undo steps for one
        // gesture). Automation-kind tracks are not recordable and are skipped.
        synth::TrackId armedTrack;
        synth::TrackKind armedKind = synth::TrackKind::Midi;
        for (const auto& track : timelineDoc.getTracks()) {
            if (track.armed && (track.kind == synth::TrackKind::Midi || track.kind == synth::TrackKind::Audio)) {
                armedTrack = track.id;
                armedKind = track.kind;
                break;
            }
        }
        const bool anyArmed = armedTrack.isValid();

        // An audio take's tap and its destination files are resolved BEFORE the transport
        // moves, for the same reason as always — a request that cannot be honoured must not leave
        // the transport rolling. Only reachable with an armed Audio track; with nothing armed (or a
        // MIDI track armed) there is no take to resolve here.
        const bool isAudioTake = anyArmed && (armedKind == synth::TrackKind::Audio);
        AudioTake take;
        RecordTapModule* tapModule = nullptr;
        if (isAudioTake) {
            auto* tapNode = ensureMasterRecordTap();
            tapModule = tapNode != nullptr ? dynamic_cast<RecordTapModule*>(tapNode->getProcessor()) : nullptr;
            if (tapModule == nullptr) {
                timelinePanel.getTransportBar().setRecordingState(false);
                statusBar.showMessage("Can't record audio: no Audio Output in the patch");
                return;
            }
            take.track = armedTrack;
            take.tapNode = tapNode->nodeID;
            if (!chooseTakeFiles(take)) {
                timelinePanel.getTransportBar().setRecordingState(false);
                statusBar.showMessage("Can't record audio: could not create the take file");
                return;
            }
        }

        // Record implies roll (DAW convention): starting a take also starts the transport if it
        // isn't already running.
        auto& transport = audioEngine.getTransport();
        const auto snap = transport.getPositionSnapshot();
        const int countInBars = timelinePanel.getTransportBar().getCountInBars();

        // Count-in pre-roll, only from a full stop — a record engaged while already playing
        // gets no pre-roll (the user is already mid-performance) and no forced click, matching
        // today's plain "record implies roll" behaviour exactly.
        double punchInBeat = snap.ppq;
        if (!snap.playing && countInBars > 0) {
            const double beatsPerBar =
                (double)snap.timeSigNumerator * 4.0 / (double)std::max(1, snap.timeSigDenominator);
            const double preRollStart = std::max(0.0, punchInBeat - (double)countInBars * beatsPerBar);
            transport.locateBeat(preRollStart);
            // Forced audible through the pre-roll regardless of the user's own metronome toggle;
            // cleared by the 10 Hz poll once the transport reaches punchInBeat (see timerCallback)
            // and unconditionally by commitMidiRecording() on stop.
            audioEngine.getMetronome().setForcedOn(true);
            transport.play();
        } else {
            if (!snap.playing)
                transport.play();
            punchInBeat = transport.getPositionSnapshot().ppq;
        }

        if (isAudioTake) {
            // The capture starts HERE, at record-on — not on the 10 Hz poll, which is what
            // used to cost a take up to ~100 ms of head. The count-in's pre-roll is therefore
            // RECORDED, and excluded from the committed clip by a trim (see commitAudioRecording);
            // the tap itself reports the exact transport sample its frame 0 landed on, so the clip's
            // placement is sample arithmetic rather than a poll observation.
            //
            // Deliberately AFTER the locate/play posted above, not before: those are transport
            // commands that take effect at the top of a block, and a frame captured before a locate
            // belongs to the OLD position — which would break the one thing the anchor promises,
            // that take frame `f` sits at `captureStart + f`. Starting after them can cost at most
            // the one block that may already be in flight (~10 ms at 512/48k), and that block is
            // pre-roll, honestly accounted for either way.
            const double rate = snap.sampleRate > 0.0 ? snap.sampleRate : 44100.0;
            if (!tapModule->startCapture(take.wavFile, take.peaksFile, rate, RecordTapModule::kNumChannels)) {
                // The tap vanished, or the file could not be opened. Nothing to salvage.
                timelinePanel.getTransportBar().setRecordingState(false);
                audioEngine.getMetronome().setForcedOn(false);
                statusBar.showMessage("Can't record audio: the take file could not be opened");
                return;
            }
            take.punchInBeat = punchInBeat;
            take.capturing = true;
            // Frozen NOW, not re-read at commit time — see the AudioTake field comments.
            take.captureSampleRate = rate;
            take.captureBpm = snap.bpm > 0.0 ? snap.bpm : 120.0;
            take.captureRecordingLatencySamples = audioEngine.getRecordingLatencySamples();
            audioTake_ = take;
        } else if (anyArmed) {
            // punchInBeat is BOTH the recorder's own bookkeeping and the audio-thread filter
            // threshold — captureBlock() drops everything before it, so the pre-roll bars the
            // performer plays along with the click are heard but never committed.
            midiRecorder.startRecording(armedTrack, punchInBeat);
        } else {
            // Nothing armed: the transport is rolling and the indicator is about to light, but no
            // take of either kind starts. Arming a track MID-ROLL does not retroactively start one
            // either — TimelineDoc::setTrackArmed() has no listener watching for this; the user has
            // to stop and press Record again once something is armed.
            statusBar.showMessage("Recording started - no track is armed");
        }

        // Lit regardless of arming — a bare "record" is still record-on.
        timelinePanel.getTransportBar().setRecordingState(true);
    };

    addAndMakeVisible(toggleTimelineButton);
    toggleTimelineButton.setComponentID("toggleTimeline");
    toggleTimelineButton.onClick = [this] {
        isTimelineVisible = !isTimelineVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("timelinePanelVisible", isTimelineVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        beginPanelSlide();
    };

    addAndMakeVisible(toggleMinimapButton);
    toggleMinimapButton.setComponentID("toggleMinimap");
    toggleMinimapButton.onClick = [this] {
        graphEditor.toggleMinimapVisibility();
        appProperties.getUserSettings()->setValue("minimapVisible", graphEditor.isMinimapVisible() ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
    };

    addAndMakeVisible(toggleModMatrixButton);
    toggleModMatrixButton.setComponentID("toggleModMatrix");
    toggleModMatrixButton.onClick = [this] {
        graphEditor.toggleModMatrixVisibility();
        applyToolbarIcons();
        resized();
    };

    addAndMakeVisible(toggleLibraryButton);
    toggleLibraryButton.setComponentID("toggleLibrary");
    toggleLibraryButton.onClick = [this] { setLibraryVisible(!isLibraryVisible); };

    addAndMakeVisible(themeToggleButton);
    themeToggleButton.setComponentID("themeToggle");
    themeToggleButton.onClick = [this] { themeManager->toggleLightDarkMode(); };

    addAndMakeVisible(autoArrangeButton);
    autoArrangeButton.setComponentID("autoArrangeButton");
    autoArrangeButton.onClick = [this] { graphEditor.autoArrange(); };

    addAndMakeVisible(settingsButton);
    settingsButton.setComponentID("settingsButton");
    settingsButton.onClick = [this]() {
        auto* settingsComp =
            new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService, aiChatComponent,
                               shortcutManager, *themeManager, &graphEditor, &accountService,
                               /*showAudioTab=*/!audioEngine.isHosted());
        settingsComp->setSize(500, 450);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(settingsComp);
        options.dialogTitle = "Settings";
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    };

    addAndMakeVisible(feedbackButton);
    feedbackButton.setComponentID("feedbackButton");
    feedbackButton.onClick = [this]() {
        auto* settingsComp =
            new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService, aiChatComponent,
                               shortcutManager, *themeManager, &graphEditor, &accountService,
                               /*showAudioTab=*/!audioEngine.isHosted(), "Feedback");
        settingsComp->setSize(500, 450);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(settingsComp);
        options.dialogTitle = "Settings";
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    };

    // Hand the (now-constructed) buttons to the toolbar for FlexBox layout. Order MUST match
    // ToolbarComponent::Slot.
    // ORDERING CONTRACT: setButtons() MUST be called BEFORE setSize() so that the first
    // resized() -> layoutButtons() pass finds the registered buttons and positions them.
    // Calling setSize() before setButtons() leaves all buttons with zero bounds on first launch.
    toolbar.setButtons({&toggleLibraryButton, &newButton, &saveButton, &loadButton, &settingsButton, &feedbackButton,
                        &undoButton, &redoButton, &autoArrangeButton, &toggleMinimapButton, &toggleModMatrixButton,
                        &toggleAiPanelButton, &toggleTimelineButton, &themeToggleButton});

    // Now that buttons are registered, trigger the first layout pass. resized() calls
    // toolbar.layoutButtons() which positions the buttons using their registered pointers.
    setSize(1600, 900);

    // Master-mute: toggles AudioEngine's master mute (audio keeps running; output is zero-filled).
    statusBar.getMasterMuteButton().setComponentID("masterMute");
    statusBar.getMasterMuteButton().onClick = [this] {
        audioEngine.setMasterMute(!audioEngine.isMasterMuted());
        statusBar.repaint();
    };

    // Status bar play/stop: the SAME TransportService actions TimelineTransportBar's own play/stop
    // button uses (see its onClick), so the transport responds identically whether the click came
    // from here or from inside the (possibly-hidden) timeline panel. "The transport is the truth" —
    // this button's visual is never flipped directly; timerCallback()'s statusBar.updateTransport()
    // call is what sets it, from the same PositionSnapshot poll TimelineTransportBar uses.
    statusBar.getTransportButton().onClick = [this] {
        auto& transport = audioEngine.getTransport();
        if (transport.getPositionSnapshot().playing)
            transport.stop();
        else
            transport.play();
    };

    // One unconditional icon/text pass at startup (subsequent calls only fire on narrow-mode flips).
    applyToolbarIcons();
    setCurrentPatchName("Default");

    // Audio device state. Guarded the same way the engine-lifecycle block below is: on the plugin
    // path the host owns the device (there is not even an Audio tab), so this app's settings file
    // has no say over it.
    //
    // ORDERING CONTRACT: both halves must be in place BEFORE audioEngine.initialise() below — the
    // saved state because initialise() is what restores it, the callback because opening a device
    // can itself broadcast a change. MainComponent owns the ApplicationProperties round trip and
    // the engine owns the device: Core never reads or writes settings.
    if (ownedAudioEngine != nullptr) {
        const juce::String savedDeviceXml =
            appProperties.getUserSettings()->getValue("audioDeviceState", juce::String());
        if (savedDeviceXml.isNotEmpty()) {
            if (auto parsed = juce::parseXML(savedDeviceXml))
                audioEngine.setSavedDeviceState(std::move(parsed));
        }

        audioEngine.onDeviceStateChanged = [this](std::unique_ptr<juce::XmlElement> state) {
            // BEFORE the null check below: a device change that JUCE does not consider
            // an explicit setup still changes how many input channels exist, and the Audio Input
            // card's jacks (plus any cable on a jack that just vanished) have to follow it.
            graphEditor.refreshIoModulesAfterDeviceChange();

            // Same reasoning for the Audio Output card's destination line (docs/layout.md —
            // module chrome): a device/rate/channel change is exactly what it needs to reflect,
            // and it must not wait for a repaint that has no other reason to happen.
            graphEditor.refreshOutputDeviceInfo();

            // Null until the user has explicitly chosen a device setup — see the declaration of
            // onDeviceStateChanged. Persisting nothing then is the point: the absent key is what
            // keeps the next launch on the inputs-off defaults.
            if (state == nullptr)
                return;
            appProperties.getUserSettings()->setValue("audioDeviceState",
                                                      state->toString(juce::XmlElement::TextFormat().singleLine()));
            appProperties.saveIfNeeded();
        };
    }

    // Output-card identity treatment: installed unconditionally (both Standalone and Hosted use
    // it — computeOutputDeviceInfoText() branches on AudioEngine::isHosted() itself), then primed
    // once below so the card is populated at startup rather than waiting for the first device
    // change, which on a fresh install may never come (see onDeviceStateChanged's own comment
    // about staying null until the user explicitly touches the Audio tab).
    graphEditor.setOutputDeviceInfoProvider([this] { return computeOutputDeviceInfoText(); });

    // Engine lifecycle is the owner's job. On the plugin path the processor already called
    // initialise() (and will call shutdown()), and its graph may already hold host-restored
    // state — re-initialising here would overwrite the user's session with the default patch.
    if (ownedAudioEngine == nullptr) {
        // Plugin path: the graph already holds the host-restored session, whose modules each carry
        // their own saved dualIO value. Forcing the preference over that would rewrite the user's
        // session, which is exactly what restoring state is supposed to avoid.
        graphEditor.updateComponents();
        graphEditor.refreshOutputDeviceInfo();
        return;
    }

    if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio) &&
        !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio)) {
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [&](bool granted) {
            if (granted) {
                audioEngine.initialise();
                applyStoredDualIOPreferenceToPatch();
                graphEditor.updateComponents();
                graphEditor.refreshOutputDeviceInfo();
            }
        });
    } else {
        audioEngine.initialise();
        applyStoredDualIOPreferenceToPatch();
        graphEditor.updateComponents();
        graphEditor.refreshOutputDeviceInfo();
    }

    // ---- Welcome screen (T114/P8-10) ---------------------------------------------------------
    // APP-ONLY: this whole block is unreachable on the plugin path anyway (it already returned at
    // the `ownedAudioEngine == nullptr` branch above), but the explicit guard is kept as the same
    // belt-and-suspenders idiom the rest of this function uses (see the audio-device-state block
    // above) — a hosted plugin's document is host-owned via getStateInformation, so this overlay
    // must never be constructed there.
    if (ownedAudioEngine != nullptr) {
        welcomeScreen_ = std::make_unique<synth::ui::WelcomeScreenComponent>();

        welcomeScreen_->onNewProject = [this] {
            // AppCommands::newPatch's own guard ("New Patch") runs first; newPatch() itself calls
            // hideWelcomeScreen() as the LAST step of its body, so a Cancel answer never touches it.
            commandManager.invokeDirectly(AppCommands::newPatch, true);
        };
        welcomeScreen_->onOpenDefaultProject = [this] { loadPresetGuarded(0); };
        // P8-31: the welcome screen's "Open an existing project…" button opens a whole .agsproj
        // project (patch + timeline), so it routes through the project half, not the patch half.
        welcomeScreen_->onOpenExistingProject = [this] { openProjectFromFile(); };
        welcomeScreen_->onOpenRecentProject = [this](const juce::File& file) { openRecentProjectGuarded(file); };
        welcomeScreen_->onWhatsNewRequested = [this] {
            // Deliberately does NOT hide the welcome screen — the user should be able to read
            // What's New and still see/use the overlay's other options afterward.
            showWhatsNewDialog();
        };
        welcomeScreen_->onShowAtLaunchChanged = [this](bool shouldShow) {
            appProperties.getUserSettings()->setValue("showWelcomeScreenAtLaunch", shouldShow);
            appProperties.saveIfNeeded();
        };

        if (recentProjects.pruneMissing() > 0)
            saveRecentProjects();
        welcomeScreen_->setRecentProjects(recentProjects.getEntries());
        const bool showAtLaunch = appProperties.getUserSettings()->getBoolValue("showWelcomeScreenAtLaunch", true);
        welcomeScreen_->setShowAtLaunch(showAtLaunch);
        welcomeScreen_->setLatestVersionLabel(juce::String(synth::branding::kProductName) + " " +
                                              synth::whatsnew::kReleaseTag);

        // Added LAST — JUCE paints children in addAndMakeVisible order, so this must come after
        // every other addAndMakeVisible() call above to sit on top of the toolbar/canvas.
        addAndMakeVisible(*welcomeScreen_);
        welcomeScreen_->setVisible(showAtLaunch);
        // setSize(1600, 900) above already ran resized() once, before this component existed — a
        // child added afterwards starts at zero bounds and would otherwise sit unsized until the
        // next real window resize. resized() itself is idempotent (every other panel's layout is
        // fraction-driven off already-settled state), so re-running it here just to size this one
        // new child is safe.
        resized();
    }

    // ---- T159: focus-region registry ---------------------------------------------------------
    // Registered unconditionally (app AND plugin path — the plugin has every one of these panels
    // too, just no welcomeScreen_) after every region root above is fully constructed and wired.
    // Order matches the Tab-cycle order docs/shortcuts.md documents: Toolbar, Library, Canvas,
    // Timeline, AI Panel, Mod Matrix. Wraps the getters/toggles that already exist rather than
    // migrating them to a new unified visibility enum — see Source/UI/Layout/FocusRegion.h's own header
    // comment.
    // The toolbar is chrome, always visible in both the app and plugin editor -- no closed state,
    // same as the canvas below, and no direct-focus shortcut targets it (Tab-cycling only).
    focusRegions_.addRegion({"toolbar", &toolbar, nullptr, nullptr});
    focusRegions_.addRegion(
        {"library", &moduleLibrary, [this] { return isLibraryVisible; }, [this] { setLibraryVisible(true); }});
    // The canvas has no closed state at all -- null isOpen/open, so it is always in the open list.
    focusRegions_.addRegion({"canvas", &graphEditor, nullptr, nullptr});
    focusRegions_.addRegion({"timeline", &timelinePanel, [this] { return isTimelineVisible; },
                             [this] {
                                 if (!isTimelineVisible && toggleTimelineButton.onClick)
                                     toggleTimelineButton.onClick();
                             }});
    focusRegions_.addRegion({"aiPanel", &aiChatComponent, [this] { return isAiPanelVisible; },
                             [this] {
                                 if (!isAiPanelVisible && toggleAiPanelButton.onClick)
                                     toggleAiPanelButton.onClick();
                             }});
    // No `open` callback: T159 wires no direct-focus shortcut to the Mod Matrix (out of scope per
    // the task), and Tab-cycling never opens a closed region — see FocusRegionRegistry::cycleFocus.
    focusRegions_.addRegion(
        {"modMatrix", &graphEditor.getModMatrix(), [this] { return graphEditor.isModMatrixVisible(); }, nullptr});

    // Repaint whichever region gains/loses focus — see FocusRegion.h's comment on
    // paintFocusRegionOutline for why nothing repaints on its own. Removed in the destructor.
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

MainComponent::~MainComponent() {
    // FRO44: unregister FIRST, before anything below (closing native plugin-editor windows
    // included) has a chance to pump the message loop. A `pluginScanCompleted` queued by a scan on
    // another thread would otherwise land mid-destruction and call `savePluginScanList()` /
    // `refreshPluginLibrary()` / `statusBar.showMessage()` on a half-torn-down component. On the
    // adopted-service path (plugin editors) the service is the PROCESSOR's and outlives this
    // destructor regardless, so leaving the listener registered would also call back into a dead
    // MainComponent the next time some other editor or the owner triggers a scan.
    getPluginScanService().removeListener(this);

    // Pairs with the addFocusChangeListener(this) at the end of initialiseCommon(). Desktop is a
    // process-global broadcaster that outlives this component, so an unremoved listener would call
    // back into freed memory on the very next focus change anywhere in the process.
    juce::Desktop::getInstance().removeFocusChangeListener(this);

    // Every plugin editor window must die before the graph/engine below do — see
    // HostedPluginWindowManager's class comment (this explicit call is one of two independent
    // safeguards; declaration order is the other).
    pluginWindowManager.closeAll();

    // Uninstall what installHostedPluginObservers() installed. Both callbacks capture
    // `this`, and on the plugin path the engine — and every hosted module in its graph — OUTLIVES
    // this editor-owned component (hosts close and reopen editors freely), so a latency change or
    // publish after this destructor would otherwise call through freed memory inside the host.
    for (auto* node : audioEngine.getGraph().getNodes()) {
        if (node == nullptr)
            continue;
        if (auto* hosted = dynamic_cast<synth::HostedPluginModule*>(node->getProcessor())) {
            hosted->onLatencyChanged = nullptr;
            hosted->onInstancePublished = nullptr;
        }
    }

    // The process-wide backend holds a bare pointer to our scan service, so unhook it before
    // anything else that could resolve an identity — a hosted-plugin restore after this point would
    // otherwise go through freed memory. Guarded on "still ours" because a second MainComponent
    // (tests construct several) will have replaced it — and because on the plugin path the installed
    // service is the PROCESSOR's, adopted rather than owned, and must survive this editor closing.
    if (auto* backend = dynamic_cast<synth::DefaultHostedPluginBackend*>(&synth::HostedPluginBackend::getDefault()))
        if (backend->getScanService() == &pluginScanService)
            backend->setScanService(nullptr);
    pluginScanService.cancelScan();

    // Unsubscribe before the manager (or our owned copy) is torn down.
    if (themeManager != nullptr)
        themeManager->removeChangeListener(this);
    // Same reason, one level down: timelinePanel is declared BEFORE shortcutManager, so member
    // teardown destroys the manager first — detach the panel's ChangeListener subscription (its
    // dynamic tooltip refresh) while the manager is still alive, or ~TimelinePanelComponent()
    // dereferences a dangling pointer on quit.
    timelinePanel.setShortcutManager(nullptr);
    // Same reason: the settings file outlives this component on the plugin path (it is a shared
    // location — see synth::userSettingsOptions()), so a write from any other holder after this
    // point would call back into freed memory.
    if (auto* settings = appProperties.getUserSettings())
        settings->removeChangeListener(this);
    // Same reason: undoManager outlives this call (its own destructor runs after this body), so a
    // stray perform/undo/redo between now and then must not reach a callback that touches
    // half-torn-down members.
    undoManager.getUndoManager().removeChangeListener(this);
    stopTimer();
    aiService.removeListener(this);
    // Order matters: stop listening to the doc first (nothing may republish while we tear down),
    // drop the panel's view of it, unhook the engine from the recorder's audio-visible state, and
    // only then detach the recorder — which commits anything still in flight into the doc and the
    // undo manager, both of which are still alive here (and outlive this body; see the declaration
    // order note in MainComponent.h). Finally drop the undo hooks: they reach back into members
    // that are about to go.
    timelineDoc.removeListener(this);
    timelinePanel.setTimelineDoc(nullptr);
    audioEngine.setAutomationRecorder(nullptr);
    audioEngine.setMidiCaptureSink(nullptr);
    automationRecorder.detach();
    undoManager.setRestoreHooks({}, {});
    graphEditor.detachAllModuleComponents();
    // Only tear down an engine we own. On the plugin path the processor's engine must survive
    // the editor being closed and reopened.
    if (ownedAudioEngine != nullptr) {
        // Drop the device-state callback first — it captures `this`, and shutdown() is the
        // call that unsubscribes the engine from its device manager.
        audioEngine.onDeviceStateChanged = nullptr;
        audioEngine.shutdown();
    }
}
