// MainComponentSetup.cpp -- initialiseCommon()'s general/app-lifecycle setup steps: panel and
// graph-editor preference restore, AI provider/account wiring, plugin-scan + recent-projects
// restore, command/shortcut registration, audio-engine bring-up and the focus-region registry.
// Split out of the former single MainComponent.cpp (FRO76) -- see MainComponent::initialiseCommon
// in MainComponent.cpp for the ordered call sequence these steps implement.
#include "AI/AIProviderRegistry.h"
#include "MainComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "UI/Settings/PreferencesSettingsTab/PreferencesSettingsTab.h"

void MainComponent::restorePanelPreferences() {
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
}

void MainComponent::restoreGraphEditorPreferences() {
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
}

void MainComponent::configureAiProvider(std::unique_ptr<synth::AIProvider> provider,
                                        synth::AIProviderRegistry registry) {
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
}

void MainComponent::wireAiChatAndAccount() {
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
}

void MainComponent::wireGraphEditorCallbacks() {
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
}

void MainComponent::wirePluginScanAndRecents() {
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
}

void MainComponent::wireCommandsAndShortcuts() {
    // Register commands for the macOS native menu bar (Edit→Undo shows Cmd+Z).
    // Do NOT add commandManager.getKeyMappings() as a KeyListener — it intercepts
    // keys like Cmd+Shift+Z and silently fails to invoke the command, preventing
    // our keyPressed() fallback from running. All key dispatch goes through keyPressed().
    commandManager.registerAllCommandsForTarget(this);
    commandManager.setFirstCommandTarget(this);
    shortcutManager.onBindingsChanged = [this] { updateCommandShortcuts(); };
    startTimerHz(10);
}

// Returns false exactly where initialiseCommon() used to `return;` early on the plugin path
// (ownedAudioEngine == nullptr) — the caller mirrors that with `if (!initialiseAudioEngine())
// return;`. True means the rest of initialiseCommon() (welcome screen, focus regions) still runs.
bool MainComponent::initialiseAudioEngine() {
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
        return false;
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
    return true;
}

void MainComponent::registerFocusRegions() {
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
