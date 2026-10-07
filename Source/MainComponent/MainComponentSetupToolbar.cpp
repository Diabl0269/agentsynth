// MainComponentSetupToolbar.cpp -- initialiseCommon()'s canvas/toolbar/status-bar chrome setup
// steps (buttons, the load menu, toolbar assembly, status bar) and the app-only welcome-screen
// setup. Split out of MainComponent.cpp -- see
// MainComponent::initialiseCommon in MainComponent.cpp for the ordered call sequence.
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "MainComponent.h"
#include "MainComponentShortcutHints.h"
#include "UI/Layout/PopupMotion.h"
#include "UI/Mixer/MixerPanelComponent/MixerPanelComponent.h"
#include "UI/Settings/SettingsWindow.h"
// Generated at CMake CONFIGURE time from local git history -- see the root CMakeLists.txt's
// "What's New" block. ${CMAKE_BINARY_DIR}/generated is on AppUI's private include path.
#include "WhatsNewData.h"

void MainComponent::addCanvasAndPanels() {
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
        // The passes below are part of the full reconcile; skipped when one follows (fullReconcileFollows()).
        if (fullReconcileFollows()) {
            bottomDock.getMidiRemotePanel().scheduleLiveRefresh();
            return;
        }
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
        // A cable patched or deleted on the canvas adds or drops a lane's modulator row; the rows are derived
        // from the graph, so they are re-derived on every structural change (cheap: a few lanes on screen).
        timelinePanel.refreshModulators();
        // A canvas delete orphans MIDI Remote assignments; the open panel shows "(missing module)"
        // only once it rebuilds. Deferred and coalesced, so a burst of structural changes is one rebuild.
        bottomDock.getMidiRemotePanel().scheduleLiveRefresh();
        // The other half of onBeforeDetachAllModuleComponents. A single-node removal
        // (deleteSelection, requestDeleteModule, replaceModule) unbinds the WHOLE mixer before it
        // frees anything, and has no rebuild of its own -- reconcileTimelineBindingsOnly() above
        // deliberately never rebuilds the mixer. Left alone, every fader would sit on screen
        // attached to nothing until an unrelated later change rebuilt the panel. This re-binds
        // against the settled graph, and is a no-op unless the columns are actually detached, so
        // it does NOT turn every structural change into a full mixer rebuild. Covers the
        // "both places" mirror view too, when one is open.
        bottomDock.rebuildIfUnboundMixerViews();
    };
    addAndMakeVisible(aiChatComponent);
    aiChatComponent.setVisible(isAiPanelVisible);
    moduleLibrary.setVisible(isLibraryVisible);
    // Added unconditionally — isBottomDockVisible stays false forever in a flag-OFF build
    // (the only code that ever flips it, the toggle button's onClick, is gated below), so this is
    // an inert invisible child there, same as any other never-shown component.
    // bottomDock is timelinePanel's new direct parent -- it adds timelinePanel as
    // ITS OWN child in its constructor (docs/mixer/panel.md#what-the-mixer-shows), so MainComponent
    // parents bottomDock here instead of timelinePanel directly. Unchanged: which of the dock's two
    // tabs is visible stays keyed to isBottomDockVisible/timelineSlide_ regardless of active tab.
    addAndMakeVisible(bottomDock);
    bottomDock.setVisible(isBottomDockVisible);
    // The Mixer's "Own panel" placement is a second independent bottom strip --
    // added here (Own-panel visibility is unrelated to isBottomDockVisible above) and given real
    // bounds only by resized(), gated on mixerPlacement_.isOwnPanelShowing().
    addAndMakeVisible(mixerPlacement_);
    // The Own panel's slide/height changes are laid out by THIS component's carve.
    mixerPlacement_.onLayoutNeeded = [this] { resized(); };
    graphEditor.getModMatrix().setVisible(graphEditor.isModMatrixVisible());
}

void MainComponent::addToolbarChrome() {
    // Z-ORDER CONSTRAINT: add the toolbar strip + status bar BEFORE the toolbar buttons.
    // JUCE paints children in addAndMakeVisible order, so the toolbar background must be
    // registered first (the buttons are direct children of MainComponent and paint on top).
    addAndMakeVisible(toolbar);
    addAndMakeVisible(statusBar);
}

void MainComponent::addFileButtons() {
    addAndMakeVisible(newButton);
    newButton.setComponentID("newButton");
    newButton.onClick = [this] { commandManager.invokeDirectly(AppCommands::newPatch, true); };

    addAndMakeVisible(saveButton);
    saveButton.setComponentID("saveButton");
    saveButton.onClick = [this] { performSaveProject(false); };

    addAndMakeVisible(loadButton);
    loadButton.setComponentID("loadButton");
    loadButton.onClick = [this] { showLoadMenu(); };
}

// The Load button's popup menu body, extracted out of addFileButtons().
void MainComponent::showLoadMenu() {
    juce::PopupMenu menu;
    auto presets = synth::PresetManager::getPresetList();
    auto categories = synth::PresetManager::getCategories();
    // Recent projects, pruned of anything that vanished from disk since the last build (a moved
    // or deleted bundle) — this is also the only time the pruned list needs re-persisting — so
    // it is gathered first regardless of where it is shown.
    if (recentProjects.pruneMissing() > 0)
        saveRecentProjects();
    auto recents = recentProjects.getEntries();

    // The flat "Open Project / Open Patch / Recent Projects" layout made a patch and a
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
            // openRecentProjectGuarded (shared with the welcome screen's recent-project rows),
            // which re-adds it (moving it back to the front) on success.
            openRecentProjectGuarded(recents[index]);
        } else if (result > 0) {
            // Shared with the welcome screen's "Open our default project" button
            // — see loadPresetGuarded.
            loadPresetGuarded(result - 1);
        }
    });
}

void MainComponent::addUndoButtons() {
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
}

void MainComponent::addToolbarToggleButtons() {
    addAndMakeVisible(toggleBottomPanelButton);
    toggleBottomPanelButton.setComponentID("toggleBottomPanel");
    toggleBottomPanelButton.onClick = [this] {
        isBottomDockVisible = !isBottomDockVisible;
        // Persist BEFORE the slide so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue(kBottomDockVisibleSettingKey, isBottomDockVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        // A deliberate open/close always supersedes the auto-hide/auto-reopen bookkeeping
        // the detach-state handler uses (see bottomDockAutoHiddenByEmptyTabs_'s own comment).
        bottomDockAutoHiddenByEmptyTabs_ = false;
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
    settingsButton.onClick = [this]() { launchSettingsWindow({}); };

    addAndMakeVisible(feedbackButton);
    feedbackButton.setComponentID("feedbackButton");
    feedbackButton.onClick = [this]() { launchSettingsWindow("Feedback"); };
}

// The visible text of a toggle says what a press does ("Hide Library" / "Show Library"), so that same
// string is its screen-reader name in every width: narrow mode only drops the visible text, and the
// toggles are not click-toggling buttons, so a screen reader gets no checked state to carry it.
void MainComponent::applyToolbarLabels(bool iconOnly) {
    const bool darkTheme = themeManager != nullptr && themeManager->getActiveTheme().isDark;
    const std::pair<juce::DrawableButton*, juce::String> labels[] = {
        {&toggleLibraryButton, isLibraryVisible ? "Hide Library" : "Show Library"},
        {&newButton, "New"},
        {&saveButton, "Save"},
        {&loadButton, "Load"},
        {&settingsButton, "Settings"},
        {&feedbackButton, "Feedback"},
        {&undoButton, "Undo"},
        {&redoButton, "Redo"},
        {&autoArrangeButton, "Auto Arrange"},
        {&toggleMinimapButton, graphEditor.isMinimapVisible() ? "Hide Minimap" : "Show Minimap"},
        {&toggleModMatrixButton, graphEditor.isModMatrixVisible() ? "Hide Matrix" : "Show Matrix"},
        {&toggleAiPanelButton, isAiPanelVisible ? "Hide AI" : "Show AI"},
        {&toggleBottomPanelButton, isBottomDockVisible ? "Hide Panel" : "Show Panel"},
        {&themeToggleButton, darkTheme ? "Light Mode" : "Dark Mode"},
    };
    for (const auto& [button, label] : labels) {
        button->setTitle(label);
        button->setButtonText(iconOnly ? juce::String() : label);
    }
}

// One place for both entry points (the gear and the feedback button), which differ only in the tab
// they open on. The Audio tab's caption names the profiled MIDI controllers; a Host MIDI
// profile has no device of its own, so it is left out.
void MainComponent::launchSettingsWindow(const juce::String& initialTabName) {
    std::vector<juce::String> profiledDevices;
    for (const auto& profile : midiLearnController_.getProfiles())
        if (profile.input.identifier != synth::midi::hostSourceKey())
            profiledDevices.push_back(profile.input.name);

    auto* settingsComp =
        new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService, aiChatComponent, shortcutManager,
                           *themeManager, &graphEditor, &accountService,
                           /*showAudioTab=*/!audioEngine.isHosted(), initialTabName, std::move(profiledDevices));
    settingsComp->setSize(SettingsWindow::kDefaultWidth, SettingsWindow::kDefaultHeight);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(settingsComp);
    options.dialogTitle = "Settings";
    options.componentToCentreAround = this;
    options.useNativeTitleBar = true;
    options.resizable = true;
    if (auto* window = synth::ui::PopupMotion::launchDialog(options))
        window->setResizeLimits(SettingsWindow::kMinWidth, SettingsWindow::kMinHeight, 4096, 4096);
}

void MainComponent::assembleToolbar() {
    // Hand the (now-constructed) buttons to the toolbar for FlexBox layout. Order MUST match
    // ToolbarComponent::Slot.
    // ORDERING CONTRACT: setButtons() MUST be called BEFORE setSize() so that the first
    // resized() -> layoutButtons() pass finds the registered buttons and positions them.
    // Calling setSize() before setButtons() leaves all buttons with zero bounds on first launch.
    toolbar.setButtons({&toggleLibraryButton, &newButton, &saveButton, &loadButton, &settingsButton, &feedbackButton,
                        &undoButton, &redoButton, &autoArrangeButton, &toggleMinimapButton, &toggleModMatrixButton,
                        &toggleAiPanelButton, &toggleBottomPanelButton, &themeToggleButton});

    // Now that buttons are registered, trigger the first layout pass. resized() calls
    // toolbar.layoutButtons() which positions the buttons using their registered pointers.
    setSize(1600, 900);

    // Cmd-hold shortcut hints: which buttons carry a key-cap bubble, and the action behind each. The
    // overlay reads the current key from shortcutManager every time it appears. Feedback and the theme
    // toggle have no shortcut action, so they are simply not listed.
    synth::ui::MainWindowHintParts hintParts;
    hintParts.buttons = {{&toggleLibraryButton, "toggleLibrary"},
                         {&newButton, "newPatch"},
                         {&saveButton, "savePreset"},
                         {&loadButton, "openProject"},
                         {&settingsButton, "openSettings"},
                         {&undoButton, "undo"},
                         {&redoButton, "redo"},
                         {&autoArrangeButton, "autoArrange"},
                         {&toggleMinimapButton, "toggleMinimap"},
                         {&toggleModMatrixButton, "toggleModMatrix"},
                         {&toggleAiPanelButton, "toggleAiPanel"},
                         {&toggleBottomPanelButton, "toggleBottomPanel"},
                         {&bottomDock.getMixerPanel().getSidePaneButton(), "toggleSidePane"},
                         {&timelinePanel.getSidePaneButton(), "toggleSidePane"},
                         {&statusBar.getTransportButton(), "togglePlayback"}};
    for (const auto& target : timelinePanel.getShortcutHintTargets())
        hintParts.buttons.push_back(target);
    hintParts.dock = &bottomDock;
    hintParts.transport = &timelinePanel.getTransportBar();
    hintParts.pianoRoll = &timelinePanel.getPianoRoll();
    hintParts.mixer = &bottomDock.getMixerPanel();
    hintParts.statusBar = &statusBar;
    hintParts.toggleBottomPanelButton = &toggleBottomPanelButton;
    hintParts.isDockOpen = [this] { return isBottomDockVisible; };
    shortcutHints_ = synth::ui::makeMainWindowShortcutHints(*this, shortcutManager, std::move(hintParts));
}

void MainComponent::wireStatusBar() {
    // Master-mute: toggles AudioEngine's master mute (audio keeps running; output is zero-filled).
    statusBar.getMasterMuteButton().setComponentID("masterMute");
    // The ctor's "MasterMute" component name otherwise leaks as the AX title via
    // ButtonAccessibilityHandler::getTitle()'s getButtonText() fallback.
    statusBar.getMasterMuteButton().setTitle("Mute master");
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
}

void MainComponent::createWelcomeScreen() {
    // ---- Welcome screen ----------------------------------------------------------------------
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
        // The welcome screen only ever offers this button before any project is open, so
        // it is always a fresh, unsaved document -- pass isNewDocument=true so it lands on
        // Compensated. The Load menu's own equivalent call (loadPresetGuarded(result - 1) above)
        // deliberately omits this: that one swaps a factory preset into a project that may already
        // be open, and must leave its pan law alone.
        welcomeScreen_->onOpenDefaultProject = [this] { loadPresetGuarded(0, /*isNewDocument=*/true); };
        // The welcome screen's "Open an existing project…" button opens a whole .agsproj
        // project (patch + timeline), so it routes through the project half, not the patch half.
        welcomeScreen_->onOpenExistingProject = [this] { openProjectFromFile(); };
        welcomeScreen_->onOpenRecentProject = [this](const juce::File& file) { openRecentProjectGuarded(file); };
        welcomeScreen_->onContributeRequested = [this] { openContributePage(); };
        welcomeScreen_->onWhatsNewRequested = [this] {
            // Deliberately does NOT hide the welcome screen — the user should be able to read
            // What's New and still see/use the overlay's other options afterward.
            showWhatsNewDialog();
        };

        if (recentProjects.pruneMissing() > 0)
            saveRecentProjects();
        welcomeScreen_->setRecentProjects(recentProjects.getEntries());
        welcomeScreen_->setLatestVersionLabel(juce::String(synth::branding::kProductName) + " " +
                                              synth::whatsnew::kReleaseTag);

        // Added LAST — JUCE paints children in addAndMakeVisible order, so this must come after
        // every other addAndMakeVisible() call above to sit on top of the toolbar/canvas.
        addAndMakeVisible(*welcomeScreen_);
        welcomeScreen_->setVisible(true); // always shown at launch: no opt-out. A legacy
                                          // "showWelcomeScreenAtLaunch" in old settings files is ignored.
        // setSize(1600, 900) above already ran resized() once, before this component existed — a
        // child added afterwards starts at zero bounds and would otherwise sit unsized until the
        // next real window resize. resized() itself is idempotent (every other panel's layout is
        // fraction-driven off already-settled state), so re-running it here just to size this one
        // new child is safe.
        resized();
    }
}
