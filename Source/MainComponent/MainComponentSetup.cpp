// MainComponentSetup.cpp -- initialiseCommon()'s general/app-lifecycle setup steps: panel and
// graph-editor preference restore, AI provider/account wiring, plugin-scan + recent-projects
// restore, command/shortcut registration, audio-engine bring-up and the focus-region registry.
// Split out of the former single MainComponent.cpp (FRO76) -- see MainComponent::initialiseCommon
// in MainComponent.cpp for the ordered call sequence these steps implement.
#include "AI/AIProviderRegistry.h"
#include "MainComponent.h"
#include "Plugin/Hosting/HostedPluginModule.h"
#include "ShortcutManager/AppCommands.h"
#include "UI/Mixer/MixerPanelComponent/MixerFocusRegion.h"
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
    graphEditor.getSmartConnections().setSmartConnectionMode(GraphEditor::smartConnectionModeFromString(
        appProperties.getUserSettings()->getValue("smartConnectionMode", "NewAndUnwired")));
    graphEditor.setDoubleClickPortDisconnectEnabled(
        appProperties.getUserSettings()->getBoolValue("doubleClickPortDisconnect", true));
    // T148 (docs/macros/auto-ports.md#ports-on-a-cable-drag): both default ON — see PreferencesSettingsTab's own toggle
    // comments for why these are plain on/off rather than the tri-state macroAutoPortPreference.
    graphEditor.setAutoCreateMacroPortsOnDragEnabled(
        appProperties.getUserSettings()->getBoolValue("macroAutoCreatePortsOnDrag", true));
    graphEditor.setAutoDeleteMacroPortsOnLastCableEnabled(
        appProperties.getUserSettings()->getBoolValue("macroAutoDeletePortsOnLastCable", true));
    // T184 (P9-3c, docs/mixer/mixer.md#channels-follow-audio-not-tracks "main workflow"): default ON — see
    // PreferencesSettingsTab's own toggle comment for why this is a plain on/off rather than a tri-state preference.
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
    // Module-card right-click MIDI Learn (FRO130) -- all three forward to the one collaborator
    // that owns RemoteEngine/midiRemoteDoc access; see MidiLearnController.h.
    graphEditor.onQueryMidiMappingsForNode = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        return midiLearnController_.queryMappings(nodeId);
    };
    graphEditor.onMidiLearnRequested = [this](juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
        midiLearnController_.arm(nodeId, paramId);
    };
    graphEditor.onMidiForgetRequested = [this](juce::AudioProcessorGraph::NodeID nodeId, const juce::String& paramId) {
        midiLearnController_.forget(nodeId, paramId);
    };
    // FRO131 decision (2026-09-22): "Edit MIDI assignment..." -- open the dock (same sequence
    // performToggleMidiRemotePanel()'s own "closed" branch runs, mirroring
    // trackChannelLink_.setMixerRevealHook()'s own "open before reveal" shape above) before asking
    // the panel to select the assignment; a closed dock has nothing on screen to select into yet.
    graphEditor.onEditMidiAssignmentRequested = [this](juce::AudioProcessorGraph::NodeID nodeId,
                                                       const juce::String& paramId) {
        auto* node = audioEngine.getGraph().getNodeForId(nodeId);
        const juce::String nodeUuid = node != nullptr ? node->properties["uuid"].toString() : juce::String();
        if (nodeUuid.isEmpty())
            return;
        if (!isTimelineVisible) {
            isTimelineVisible = true;
            appProperties.getUserSettings()->setValue("timelinePanelVisible", "1");
            appProperties.getUserSettings()->saveIfNeeded();
            applyToolbarIcons();
            beginPanelSlide();
        }
        mixerDock.selectMidiRemoteAssignment(nodeUuid, paramId);
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

// FRO127: wires synth::midi::RemoteEngine to the AudioEngine seam and primes it with whatever
// controller profiles and project assignments already exist. Called right after
// wireCommandsAndShortcuts() above (commandManager must exist — the action invoker dispatches
// through it) and deliberately BEFORE initialiseAudioEngine() below: that function returns early
// in Hosted mode (only the app-only welcome screen/focus regions depend on it), and MIDI Remote
// must still wire up for a hosted plugin (docs/control/midi-remote.md#the-plugin-build-vst3au-inside-a-host's
// hostSourceKey exists exactly for that case). openMidiDevicesForRemote/getOpenMidiInputIdentifiers are both
// no-ops/empty in Hosted mode regardless of whether the real audio device has been opened yet, so nothing here depends
// on initialiseAudioEngine() having run first.
void MainComponent::wireMidiRemoteEngine() {
    applyMidiRemotePreferences(); // the Preferences group's default takeover + badge switch
    remoteEngine.setActionInvoker(&remoteActionInvoker_);
    remoteEngine.setActionCommandLookup(
        [](const juce::String& actionId) { return AppCommands::getCommandForAction(actionId); });
    // The engine yields exactly as a second mouse would while a real gesture already holds the
    // same parameter (docs/control/midi-remote.md#how-does-a-hardware-value-reach-a-parameter) —
    // GestureClaims::isClaimed is already exactly the audio-visible predicate AutomationApplier itself consults, so no
    // new plumbing is needed here.
    remoteEngine.setParameterClaimedPredicate([this](const juce::AudioProcessorParameter* param) {
        return automationRecorder.getAudioState().claims.isClaimed(param);
    });

    // Profiles are loaded once by MidiLearnController's own construction (a member declared right
    // after remoteEngine, so it is already alive here) -- this just republishes that same load.
    const auto& profiles = midiLearnController_.getProfiles();
    remoteEngine.setProfiles(profiles);
    remoteEngine.setAssignments(midiRemoteDoc.assignments);

    std::vector<juce::String> deviceNames;
    deviceNames.reserve(profiles.size());
    for (const auto& profile : profiles)
        deviceNames.push_back(profile.input.name);
    audioEngine.openMidiDevicesForRemote(deviceNames); // no-op in Hosted mode

    auto sources = audioEngine.getOpenMidiInputIdentifiers(); // empty in Hosted mode
    if (audioEngine.isHosted())
        sources.push_back(synth::midi::hostSourceKey());
    remoteEngine.setSources(sources);

    // FRO262: the priming above only ever runs once, here. A device ticked in the Audio tab (or one
    // that reappears after a reconnect) afterwards needs the SAME re-registration --
    // AudioEngine::reconcileMidiInputs() (changeListenerCallback) decides WHICH devices end up
    // open, and refreshSources() just republishes the resulting set through the identical
    // getOpenMidiInputIdentifiers -> RemoteEngine::setSources() path used above. A no-op assignment
    // in Hosted mode: the callback that would invoke it can structurally never fire there
    // (changeListenerCallback's own isHosted() guard) -- cleared in MainComponent's destructor
    // beside onDeviceStateChanged.
    //
    // FRO262 (follow-up): refreshSources() alone only fixes MIDI Learn actually hearing the device
    // -- it never told the panel. MidiRemotePanelComponent::rebuildFromProfiles() computes each
    // Controllers-list row's present/absent state from audioEngine.getOpenMidiInputIdentifiers() at
    // rebuild time, so a device that newly opens while the panel tab is already showing stayed
    // greyed until MixerDockComponent::applyTabVisibility()'s tab-switch-in catch-up ran it.
    // scheduleLiveRefresh() (FRO263) is the same deferred/coalesced entry point
    // midiLearnController_.onChanged below uses, so this reuses that seam rather than adding a
    // second seam.
    audioEngine.onMidiDevicesChanged = [this] {
        midiLearnController_.refreshSources();
        mixerDock.getMidiRemotePanel().scheduleLiveRefresh();
    };

    // FRO133: the mixer panel and transport bar are plain MainComponent members, already fully
    // constructed by the time any constructor-body wiring function runs (member-init order, not
    // this function's own call order) -- so it's safe to hand MidiLearnController their addresses
    // here regardless of whether mixerDock/timelinePanel have run their own configure() yet. See
    // MidiLearnController::setMixerPanel()/setTransportBar()'s own doc comment for why this exists:
    // GraphEditor::setMidiLearnArmed() only reaches the canvas card, not the mixer column or the
    // transport bar's own breathing outline for the SAME/an action target.
    midiLearnController_.setMixerPanel(&mixerDock.getMixerPanel());
    midiLearnController_.setTransportBar(&timelinePanel.getTransportBar());
    midiLearnController_.setPickOverlayHost(this); // FRO135: the pick-target overlay covers canvas, dock and transport
    midiLearnController_.setPickPassThrough(mixerDock.getTabButtons());
    mixerDock.onActiveTabChanged = [this] {
        midiLearnController_.refreshPickTarget();
        // Rebuilds and layout the switch queued land after this call; re-measure once they have.
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
            if (safe != nullptr)
                safe->midiLearnController_.refreshPickTarget();
        });
    };

    // FRO131: same "wire it once everything it needs is alive" reasoning as the two calls above --
    // the MIDI Remote panel needs remoteEngine/midiLearnController_/midiRemoteDoc, none of which
    // exist yet at MixerDockComponent's own construction time (see MixerDockComponent::
    // configureMidiRemote()'s doc comment).
    mixerDock.configureMidiRemote(audioEngine, remoteEngine, midiLearnController_, midiRemoteDoc, graphEditor);
    mixerDock.getMidiRemotePanel().onLocateNode = [this](const juce::String& nodeUuid) { selectNodeInGraph(nodeUuid); };

    // FRO263: keep the panel live while it's open, not just on its own tab-switch-in --
    // MidiLearnController::onChanged fires after every mutation that changes what the panel shows
    // (see its own doc comment), so wiring it here covers Learn/Forget/Undo/Redo/a panel-side profile
    // edit without a callback per mutation site. midiLearnController_ is declared after mixerDock in
    // MainComponent.h, so it destructs first -- this lambda's `this` capture never outlives mixerDock.
    midiLearnController_.onChanged = [this] { mixerDock.getMidiRemotePanel().scheduleLiveRefresh(); };

    // Transport-bar right-click MIDI Learn (FRO133) -- action targets, so these three forward to
    // MidiLearnController's action-keyed overloads rather than GraphEditor's node-keyed ones (see
    // this function's own graphEditor.onMidiLearnRequested sibling in wireGraphEditorCallbacks()).
    auto& transportBar = timelinePanel.getTransportBar();
    transportBar.onQueryMidiMappingsForActions = [this] { return midiLearnController_.queryActionMappings(); };
    transportBar.onMidiLearnRequested = [this](const juce::String& actionId) {
        midiLearnController_.armAction(actionId);
    };
    transportBar.onMidiForgetRequested = [this](const juce::String& actionId) {
        midiLearnController_.forgetAction(actionId);
    };

    // FRO253: mixer column Solo right-click MIDI Learn -- a nodeCommand target, so these three
    // forward to MidiLearnController's node-command-keyed overloads (mirrors the transport-bar
    // action wiring immediately above; unlike a parameter target, Solo has no
    // GraphEditor::onMidiLearnRequested sibling to reuse -- see MixerColumnMidiLearn.cpp).
    auto& mixerPanel = mixerDock.getMixerPanel();
    mixerPanel.onQuerySoloMidiMapping = [this](juce::AudioProcessorGraph::NodeID nodeId) -> juce::String {
        const auto mappings = midiLearnController_.queryNodeCommandMappings(nodeId);
        const auto found = mappings.find(synth::NodeCommandKind::toggleSolo);
        return found != mappings.end() ? found->second : juce::String();
    };
    mixerPanel.onSoloMidiLearnRequested = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        midiLearnController_.armNodeCommand(nodeId, synth::NodeCommandKind::toggleSolo);
    };
    mixerPanel.onSoloMidiForgetRequested = [this](juce::AudioProcessorGraph::NodeID nodeId) {
        midiLearnController_.forgetNodeCommand(nodeId, synth::NodeCommandKind::toggleSolo);
    };
    // FRO253: re-syncs the mixer column's M/S visuals after a hardware press flips solo outside
    // any column's own click -- see MixerColumnComponent::toggleSoloed's callers for why nothing
    // else does this (MixerColumnMidiLearn.cpp / RemoteActionInvokerImpl's own comment).
    remoteActionInvoker_.onNodeCommandApplied = [&mixerPanel](juce::AudioProcessorGraph::NodeID) {
        mixerPanel.refreshMuteSoloVisuals();
    };

    // Installed last: nothing may reach the sink before it has profiles/assignments/sources.
    audioEngine.setRemoteMessageSink(&remoteEngine);
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

            // Same reasoning for the Audio Output card's destination line
            // (docs/layout/module-card.md): a device/rate/channel change is exactly what it reflects,
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

    // Also installed unconditionally, same reasoning: on the Hosted path (ownedAudioEngine ==
    // nullptr) the processor -- not this MainComponent -- calls audioEngine.shutdown(), and
    // nothing enforces that it only does so after this MainComponent (and the parameter
    // attachments its GraphEditor's ModuleComponents hold) has already been destroyed. Wiring the
    // hook here, rather than only inside the `ownedAudioEngine != nullptr` block below, is what
    // closes that gap (FRO87) -- detachAllModuleComponents() runs before shutdown() frees the
    // graph's nodes/parameters no matter which path called shutdown().
    audioEngine.onBeforeShutdown = [this] { graphEditor.detachAllModuleComponents(); };

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
    rebuildFocusRegions();

    // Repaint whichever region gains/loses focus — see FocusRegion.h's comment on
    // paintFocusRegionOutline for why nothing repaints on its own. Removed in the destructor.
    // ONE-TIME registration: rebuildFocusRegions() re-runs on every detach/redock (FRO12), but
    // this listener must not — see that method's own call site (mixerDock.onPanelDetachStateChanged).
    juce::Desktop::getInstance().addFocusChangeListener(this);
}

void MainComponent::rebuildFocusRegions() {
    // ---- T159: focus-region registry ---------------------------------------------------------
    // Registered unconditionally (app AND plugin path — the plugin has every one of these panels
    // too, just no welcomeScreen_) after every region root above is fully constructed and wired.
    // Order matches the Tab-cycle order docs/control/shortcuts.md documents: Toolbar, Library, Canvas,
    // Timeline, AI Panel, Mod Matrix. Wraps the getters/toggles that already exist rather than
    // migrating them to a new unified visibility enum — see Source/UI/Layout/FocusRegion.h's own header
    // comment.
    //
    // FRO12 (P9-6): cleared and rebuilt on every call so re-running it after a detach/redock never
    // duplicates entries -- see the class-level call site in wireTimelinePanel()
    // (mixerDock.onPanelDetachStateChanged). Each hosted panel's ONE detached-window focus region
    // is registered once on its host, not here -- see DetachablePanelHost::setHostedPanelFocusRegion.
    focusRegions_.clear();

    // The toolbar is chrome, always visible in both the app and plugin editor -- no closed state,
    // same as the canvas below, and no direct-focus shortcut targets it (Tab-cycling only).
    focusRegions_.addRegion({"toolbar", &toolbar, nullptr, nullptr});
    focusRegions_.addRegion(
        {"library", &moduleLibrary, [this] { return isLibraryVisible; }, [this] { setLibraryVisible(true); }});
    // The canvas has no closed state at all -- null isOpen/open, so it is always in the open list.
    focusRegions_.addRegion({"canvas", &graphEditor, nullptr, nullptr});
    // FRO18 (plan (a)): "timeline" and "mixer" now share the SAME dock, one tab visible at a time
    // -- isTimelineVisible alone (the dock's own open/closed state) is no longer enough to say the
    // Timeline region is open, since the dock can be open on the MIXER tab instead. Both regions'
    // `open` re-select their own tab first (mirroring modMatrix's "no open state of its own to
    // open" precedent for the case that's already showing) before falling through to the shared
    // "open the dock if it's closed" step every panel toggle already does.
    //
    // FRO12: guarded -- a Timeline detached to its own window has nothing docked here to cycle
    // to; Tab inside that window cycles its OWN one-region registry instead (see
    // DetachedPanelWindow::keyPressed). Wrapping only -- never reorder/rename the regions below.
    // FRO131: the dock grew a third tab (MidiRemote) -- excluding only Mixer here is no longer
    // enough to say Timeline is the one actually showing, or this region reports open while the
    // MidiRemote tab is the one on screen.
    if (!mixerDock.getTimelineHost().isDetached())
        focusRegions_.addRegion({"timeline", &timelinePanel,
                                 [this] {
                                     return isTimelineVisible && !mixerDock.isMixerTabActive() &&
                                            !mixerDock.isMidiRemoteTabActive();
                                 },
                                 [this] {
                                     mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::Timeline);
                                     if (!isTimelineVisible && toggleTimelineButton.onClick)
                                         toggleTimelineButton.onClick();
                                 }});
    // FRO18 plan (a)'s "FRO12 seam": the actual registration (open predicate + no `open` callback
    // -- see MixerFocusRegion.h's own comment) lives in the free `registerMixerFocusRegion` helper
    // so a future detached mixer window (FRO12) can register the same region against its own
    // FocusRegionRegistry with a different `dockOpen` predicate instead of re-deriving this logic.
    //
    // FRO12: three placements, three shapes -- never reorder/rename "mixer" once registered:
    //  - Tab: same guard shape as "timeline" above; registerMixerFocusRegion's own dockOpen AND
    //    dock.isMixerTabActive() gate is exactly right here (the host is still docked, one tab
    //    visible at a time).
    //  - Window: the detached window's OWN one-region registry covers it instead (see
    //    DetachablePanelHost::setHostedPanelFocusRegion) -- no MainComponent-level region while
    //    detached, same as "timeline" while the Timeline is detached.
    //  - Own panel: still the SAME top-level window (not a DetachedPanelWindow), so it needs its
    //    OWN MainComponent-level region here too, or Tab-cycling in the main window can never
    //    reach it -- registerMixerFocusRegion's helper hardcodes dock.isMixerTabActive(), which is
    //    always false once the dock's Mixer tab is disabled for this placement (see
    //    MixerPlacementController::applyPlacement), so this is a direct addRegion against
    //    mixerPlacement_'s own visibility instead of that helper.
    if (!mixerDock.getMixerHost().isDetached()) {
        if (mixerPlacement_.getPlacement() == synth::ui::MixerPlacementController::Placement::Tab)
            synth::ui::registerMixerFocusRegion(focusRegions_, mixerDock, [this] { return isTimelineVisible; });
        else if (mixerPlacement_.getPlacement() == synth::ui::MixerPlacementController::Placement::OwnPanel)
            focusRegions_.addRegion(
                {"mixer", &mixerDock.getMixerPanel(), [this] { return mixerPlacement_.isOwnPanelShowing(); }, nullptr});
    }
    // FRO131: same guard shape as "timeline" above -- MidiRemote has no placement variant (no
    // Own-panel/Window controller like Mixer's mixerPlacement_), so it is always parented here
    // unless detached to its own window, in which case that window's own one-region registry
    // covers it (DetachablePanelHost::setHostedPanelFocusRegion, wired alongside the other two in
    // wireTimelinePanelServicesAndShortcuts() below).
    if (!mixerDock.getMidiRemoteHost().isDetached())
        focusRegions_.addRegion({"midiRemote", &mixerDock.getMidiRemotePanel(),
                                 [this] { return isTimelineVisible && mixerDock.isMidiRemoteTabActive(); },
                                 [this] {
                                     mixerDock.setActiveTab(synth::ui::MixerDockComponent::Tab::MidiRemote);
                                     if (!isTimelineVisible && toggleMidiRemoteButton.onClick)
                                         toggleMidiRemoteButton.onClick();
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
}
