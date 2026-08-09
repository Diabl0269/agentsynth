#include "MainComponent.h"
#include "AI/AIProviderRegistry.h"
#include "Branding.h"
#include "UI/SettingsWindow.h"

// ---- Primary constructor (injected ThemeManager + LookAndFeel from Main.cpp) ----
MainComponent::MainComponent(synth::theme::ThemeManager& tm, synth::theme::AppLookAndFeel& lf,
                             std::unique_ptr<synth::AIProvider> provider)
    : graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    // Setup ApplicationProperties
    propertiesOptions.applicationName = synth::branding::kProductName;
    propertiesOptions.folderName = synth::branding::kSettingsFolderName;
    propertiesOptions.filenameSuffix = "settings";
    propertiesOptions.osxLibrarySubFolder = "Application Support";
    propertiesOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Restore persisted theme and apply it. Must come AFTER appProperties is configured.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());

    // Subscribe to theme changes so we can re-skin on every switch.
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider), synth::AIProviderRegistry::createDefault());
}

// ---- Delegating constructor for tests / legacy call sites ----
MainComponent::MainComponent(std::unique_ptr<synth::AIProvider> provider, synth::AIProviderRegistry registry)
    : graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties) {
    // Own a default ThemeManager + LookAndFeel so the code behaves identically
    // to the primary-ctor path (no special-casing in the rest of the class).
    ownedThemeManager = std::make_unique<synth::theme::ThemeManager>();
    ownedLookAndFeel = std::make_unique<synth::theme::AppLookAndFeel>();
    themeManager = ownedThemeManager.get();
    lookAndFeel = ownedLookAndFeel.get();

    // Setup ApplicationProperties (same as primary ctor)
    propertiesOptions.applicationName = synth::branding::kProductName;
    propertiesOptions.folderName = synth::branding::kSettingsFolderName;
    propertiesOptions.filenameSuffix = "settings";
    propertiesOptions.osxLibrarySubFolder = "Application Support";
    propertiesOptions.storageFormat = juce::PropertiesFile::storeAsXML;
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

    // ORDERING CONTRACT: read the persisted panel-visibility flags FIRST, before any
    // setVisible()/addAndMakeVisible() call that depends on them. These override the member
    // initialisers (isLibraryVisible{true}, isAiPanelVisible=false).
    isLibraryVisible = appProperties.getUserSettings()->getBoolValue("librarySidebarVisible", true);
    isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);
    graphEditor.setAlignmentGuidesEnabled(
        appProperties.getUserSettings()->getBoolValue("alignmentGuidesEnabled", true));

    // Load AI provider preference. "ollama" is the persisted id (see AIProviderRegistry),
    // not a display name — registry.create() falls back to the first registered provider
    // if the saved id is unknown (e.g. stale pre-registry value, or empty).
    juce::String savedProviderId = appProperties.getUserSettings()->getValue("aiProvider", "ollama");
    juce::String savedOllamaHost = appProperties.getUserSettings()->getValue("ollamaHost", "http://localhost:11434");

    if (provider) {
        aiService.setProvider(std::move(provider));
    } else {
        aiService.setProvider(registry.create(savedProviderId, {savedOllamaHost, {}}));
    }

    // ORDERING CONTRACT: aiChatComponent is a member, so its constructor (which calls
    // refreshModels()) already ran BEFORE this body — at that point aiService had no
    // provider, so discovery short-circuited and no model was ever selected. We must
    // refresh again HERE, after setProvider(), or currentModel stays empty and every
    // /api/chat request is rejected by Ollama with HTTP 400 "model is required".
    // Regression: see #96 / f7cba4a.
    aiChatComponent.refreshModels();
    aiService.addListener(this);
    undoManager.setGraphEditor(&graphEditor);
    setWantsKeyboardFocus(true);
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
    graphEditor.onGraphStructureChanged = [this] { moduleLibrary.repaint(); };
    addAndMakeVisible(aiChatComponent);
    aiChatComponent.setVisible(isAiPanelVisible);
    moduleLibrary.setVisible(isLibraryVisible);
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
    saveButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                statusBar.showMessage("Saving...");
                graphEditor.savePreset(file);
                setCurrentPatchName(file.getFileNameWithoutExtension());
                statusBar.showMessage("Saved: " + file.getFileNameWithoutExtension());
            }
        });
    };

    addAndMakeVisible(loadButton);
    loadButton.setComponentID("loadButton");
    loadButton.onClick = [this] {
        juce::PopupMenu menu;
        auto presets = synth::PresetManager::getPresetList();
        auto categories = synth::PresetManager::getCategories();
        for (const auto& cat : categories) {
            juce::PopupMenu subMenu;
            for (int i = 0; i < presets.size(); ++i) {
                if (presets[i].category == cat)
                    subMenu.addItem(i + 1, presets[i].name);
            }
            menu.addSubMenu(cat, subMenu);
        }
        menu.addSeparator();
        menu.addItem(1000, "Load from file...");
        // Capture `presets` by value — the outer local is gone by the time the async callback runs.
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton), [this, presets](int result) {
            if (result == 1000) {
                openPresetFromFile();
            } else if (result > 0) {
                statusBar.showMessage("Loading preset...");
                // loadFactoryPreset detaches existing components (stopping scope timers) before the graph
                // is cleared — avoids a use-after-free where a ScopeComponent reads a freed VisualBuffer.
                graphEditor.loadFactoryPreset(result - 1);
                setCurrentPatchName(presets[result - 1].name);
                statusBar.showMessage("Loaded: " + presets[result - 1].name);
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
        const bool newVisible = !isAiPanelVisible;
        isAiPanelVisible = newVisible;
        // Persist BEFORE animation so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("aiPanelVisible", isAiPanelVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();

        auto fromResult = computePanelBounds(isLibraryVisible, !newVisible); // previous layout
        if (newVisible) {
            // Showing: make visible before animating in.
            aiChatComponent.setVisible(true);
            if (fromResult.aiPanelBounds.isEmpty())
                fromResult.aiPanelBounds =
                    juce::Rectangle<int>(fromResult.graphEditorBounds.getRight(), fromResult.graphEditorBounds.getY(),
                                         0, fromResult.graphEditorBounds.getHeight());
        }
        // Apply the FINAL layout immediately so headless tests (no VBlank) see correct bounds.
        // The animation below is cosmetic only — it starts from fromResult and converges to the
        // same toResult that resized() already applied.
        auto toResult = computePanelBounds(isLibraryVisible, newVisible);
        resized();
        if (!newVisible)
            aiChatComponent.setVisible(false);
        animatePanelTransition(fromResult, toResult, /*hideLib=*/false, /*hideAi=*/!newVisible);
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

    addAndMakeVisible(autoArrangeButton);
    autoArrangeButton.setComponentID("autoArrangeButton");
    autoArrangeButton.onClick = [this] { graphEditor.autoArrange(); };

    addAndMakeVisible(settingsButton);
    settingsButton.setComponentID("settingsButton");
    settingsButton.onClick = [this]() {
        auto* settingsComp = new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService,
                                                aiChatComponent, shortcutManager, *themeManager, &graphEditor);
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
    toolbar.setButtons({&toggleLibraryButton, &newButton, &saveButton, &loadButton, &settingsButton, &undoButton,
                        &redoButton, &autoArrangeButton, &toggleModMatrixButton, &toggleAiPanelButton});

    // Now that buttons are registered, trigger the first layout pass. resized() calls
    // toolbar.layoutButtons() which positions the buttons using their registered pointers.
    setSize(1600, 900);

    // Master-mute: toggles AudioEngine's master mute (audio keeps running; output is zero-filled).
    statusBar.getMasterMuteButton().setComponentID("masterMute");
    statusBar.getMasterMuteButton().onClick = [this] {
        audioEngine.setMasterMute(!audioEngine.isMasterMuted());
        statusBar.repaint();
    };

    // One unconditional icon/text pass at startup (subsequent calls only fire on narrow-mode flips).
    applyToolbarIcons();
    setCurrentPatchName("Default");

    if (juce::RuntimePermissions::isRequired(juce::RuntimePermissions::recordAudio) &&
        !juce::RuntimePermissions::isGranted(juce::RuntimePermissions::recordAudio)) {
        juce::RuntimePermissions::request(juce::RuntimePermissions::recordAudio, [&](bool granted) {
            if (granted) {
                audioEngine.initialise();
                graphEditor.updateComponents();
            }
        });
    } else {
        audioEngine.initialise();
        graphEditor.updateComponents();
    }
}

MainComponent::~MainComponent() {
    // Unsubscribe before the manager (or our owned copy) is torn down.
    if (themeManager != nullptr)
        themeManager->removeChangeListener(this);
    stopTimer();
    aiService.removeListener(this);
    graphEditor.detachAllModuleComponents();
    audioEngine.shutdown();
}

// ---- Theme change callback: re-skin pass ----
void MainComponent::changeListenerCallback(juce::ChangeBroadcaster* /*source*/) {
    // Push new theme values into the LookAndFeel (colours / treatment / metrics), then
    // propagate lookAndFeelChanged() + a single repaint so every widget re-skins.
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    if (auto* top = getTopLevelComponent())
        top->sendLookAndFeelChange();
    // Re-tint the toolbar / status-bar icons from the already-retinted IconLibrary cache.
    applyToolbarIcons();
    repaint();
}

void MainComponent::timerCallback() {
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());

    // Status bar polls at 5 Hz (every 2nd tick of the 10 Hz timer). update() is gated — it
    // only repaints the status bar when a displayed value actually changes. ZERO logging.
    if (++statusBarTickCount_ >= 2) {
        statusBarTickCount_ = 0;
        const float cpu = (float)(audioEngine.getDeviceManager().getCpuUsage() * 100.0);
        statusBar.update(cpu, audioEngine.getDisplayVoiceCount(), currentPatchName_);
    }
}

void MainComponent::aiPatchAboutToApply() {
    // Runs synchronously before the AI patch clears/rebuilds the graph. Detach module components now so
    // their ScopeComponent timers stop and no component references a soon-to-be-freed VisualBuffer.
    graphEditor.detachAllModuleComponents();
}

void MainComponent::aiPatchApplied() {
    setCurrentPatchName("AI Patch");
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->graphEditor.updateComponents();
    });
}

void MainComponent::simulateLoadFactoryPresetForTest(int index) {
    auto presets = synth::PresetManager::getPresetList();
    if (index < 0 || index >= presets.size())
        return;
    graphEditor.loadFactoryPreset(index);
    setCurrentPatchName(presets[index].name);
}

void MainComponent::openPresetFromFile() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{}) {
            statusBar.showMessage("Loading preset...");
            graphEditor.loadPreset(file);
            setCurrentPatchName(file.getFileNameWithoutExtension());
            statusBar.showMessage("Loaded: " + file.getFileNameWithoutExtension());
        }
    });
}

//==============================================================================
void MainComponent::paint(juce::Graphics& g) {
    // (Our component is opaque, so we must completely fill the background with a
    // solid colour)
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands) {
    commands.addArray({AppCommands::openSettings, AppCommands::savePreset, AppCommands::openPreset,
                       AppCommands::newPatch, AppCommands::undo, AppCommands::redo, AppCommands::toggleModMatrix,
                       AppCommands::toggleAiPanel, AppCommands::autoArrange, AppCommands::toggleLibrary});
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) {
    switch (commandID) {
    case AppCommands::openSettings: {
        result.setInfo("Open Settings", "Open the settings window", "General", 0);
        auto kp = shortcutManager.getBinding("openSettings");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::savePreset: {
        result.setInfo("Save Preset", "Save the current preset", "General", 0);
        auto kp = shortcutManager.getBinding("savePreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::openPreset: {
        result.setInfo("Open Preset", "Open a preset file", "General", 0);
        auto kp = shortcutManager.getBinding("openPreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::newPatch: {
        result.setInfo("New Patch", "Clear the canvas and start a new patch", "General", 0);
        auto kp = shortcutManager.getBinding("newPatch");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::undo: {
        result.setInfo("Undo", "Undo the last action", "Edit", 0);
        auto kp = shortcutManager.getBinding("undo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::redo: {
        result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
        auto kp = shortcutManager.getBinding("redo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleModMatrix: {
        result.setInfo("Toggle Mod Matrix", "Toggle the modulation matrix panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleModMatrix");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleAiPanel: {
        result.setInfo("Toggle AI Panel", "Toggle the AI chat panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleAiPanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::autoArrange: {
        result.setInfo("Auto Arrange", "Auto-arrange modules by signal flow", "View", 0);
        auto kp = shortcutManager.getBinding("autoArrange");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case AppCommands::toggleLibrary: {
        result.setInfo("Toggle Module Library", "Toggle the module library sidebar", "View", 0);
        auto kp = shortcutManager.getBinding("toggleLibrary");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    default:
        break;
    }
}

bool MainComponent::perform(const InvocationInfo& info) {
    switch (info.commandID) {
    case AppCommands::openSettings:
        if (settingsButton.onClick)
            settingsButton.onClick();
        return true;
    case AppCommands::savePreset:
        if (saveButton.onClick)
            saveButton.onClick();
        return true;
    case AppCommands::openPreset:
        openPresetFromFile();
        return true;
    case AppCommands::newPatch:
        graphEditor.newPatch();
        setCurrentPatchName("Untitled");
        statusBar.showMessage("New patch");
        return true;
    case AppCommands::undo:
        if (undoManager.canUndo())
            undoManager.undo();
        return true;
    case AppCommands::redo:
        if (undoManager.canRedo())
            undoManager.redo();
        return true;
    case AppCommands::toggleModMatrix:
        toggleModMatrixButton.triggerClick();
        return true;
    case AppCommands::toggleAiPanel:
        toggleAiPanelButton.triggerClick();
        return true;
    case AppCommands::autoArrange:
        graphEditor.autoArrange();
        return true;
    case AppCommands::toggleLibrary:
        setLibraryVisible(!isLibraryVisible);
        return true;
    default:
        return false;
    }
}

void MainComponent::updateCommandShortcuts() { commandManager.commandStatusChanged(); }

bool MainComponent::keyPressed(const juce::KeyPress& key) {
    auto action = shortcutManager.getActionForKeyPress(key);
    if (action.isEmpty())
        return false;
    auto cmdId = AppCommands::getCommandForAction(action);
    if (cmdId == 0)
        return false;
    return commandManager.invokeDirectly(cmdId, true);
}

void MainComponent::resized() {
    // CANONICAL LAYOUT (§2.4). Carve top→bottom: toolbar strip, status bar, AI panel (right,
    // if visible), library sidebar (left, if visible), canvas (remainder). Dimensions come
    // from the themed Metrics tokens, with literal fallbacks for the headless test path.
    int tbH = 36, sbH = 24;
    int libW = isLibraryVisible ? 200 : 0;
    int aiW = isAiPanelVisible ? 300 : 0;
    if (auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        tbH = m.toolbarHeight;
        sbH = m.statusBarHeight;
        libW = isLibraryVisible ? m.librarySidebarWidth : m.sidebarCollapsedWidth;
        aiW = isAiPanelVisible ? m.aiPanelWidth : 0;
    }

    auto bounds = getLocalBounds();
    auto toolbarBounds = bounds.removeFromTop(tbH);
    toolbar.setBounds(toolbarBounds);

    // Gate the Drawable clone work to narrow-mode transitions only: layoutButtons() updates
    // toolbar.isNarrowMode(); we only re-run applyToolbarIcons() when the mode actually flips.
    const bool prevNarrow = toolbarNarrowMode_;
    toolbar.layoutButtons(toolbarBounds);
    toolbarNarrowMode_ = toolbar.isNarrowMode();
    if (toolbarNarrowMode_ != prevNarrow)
        applyToolbarIcons();

    statusBar.setBounds(bounds.removeFromBottom(sbH));

    // Skip removeFromLeft/Right when hidden so we never setBounds to a zero-width rect.
    if (isAiPanelVisible)
        aiChatComponent.setBounds(bounds.removeFromRight(aiW));
    if (isLibraryVisible)
        moduleLibrary.setBounds(bounds.removeFromLeft(libW));

    graphEditor.setBounds(bounds);
}

// ---- Toolbar icon + text application ----
void MainComponent::applyToolbarIcons() {
    using synth::theme::Icon;

    auto* lf = dynamic_cast<synth::theme::AppLookAndFeel*>(&getLookAndFeel());

    // In narrow mode the buttons are 32 px wide — icon only, no text. In wide mode the
    // toggle buttons carry stateful text; the rest carry a static label.
    const bool iconOnly = toolbarNarrowMode_;

    // setImages no-ops (leaves the button blank) when the icon is absent (headless LnF null).
    auto setIcon = [&](juce::DrawableButton& b, Icon id) {
        if (lf == nullptr)
            return;
        if (auto d = lf->getIcon(id))
            b.setImages(d.get());
    };

    setIcon(toggleLibraryButton, Icon::ToggleLibrary);
    setIcon(newButton, Icon::ActionNew);
    setIcon(saveButton, Icon::ActionSave);
    setIcon(loadButton, Icon::ActionLoad);
    setIcon(settingsButton, Icon::ActionSettings);
    setIcon(undoButton, Icon::ActionUndo);
    setIcon(redoButton, Icon::ActionRedo);
    setIcon(autoArrangeButton, Icon::ActionAutoArrange);
    setIcon(toggleModMatrixButton, Icon::ToggleMatrix);
    setIcon(toggleAiPanelButton, Icon::ToggleAI);

    // Master-mute uses the transport-stop glyph (no real play/stop transport this phase).
    if (lf != nullptr)
        if (auto d = lf->getIcon(Icon::TransportStop))
            statusBar.getMasterMuteButton().setImages(d.get());

    // Text: cleared in narrow mode; stateful for the toggles in wide mode.
    newButton.setButtonText(iconOnly ? "" : "New");
    saveButton.setButtonText(iconOnly ? "" : "Save");
    loadButton.setButtonText(iconOnly ? "" : "Load Presets");
    settingsButton.setButtonText(iconOnly ? "" : "Settings");
    undoButton.setButtonText(iconOnly ? "" : "Undo");
    redoButton.setButtonText(iconOnly ? "" : "Redo");
    autoArrangeButton.setButtonText(iconOnly ? "" : "Auto Arrange");
    toggleModMatrixButton.setButtonText(iconOnly ? ""
                                                 : (graphEditor.isModMatrixVisible() ? "Hide Matrix" : "Show Matrix"));
    toggleAiPanelButton.setButtonText(iconOnly ? "" : (isAiPanelVisible ? "Hide AI" : "Show AI"));
    toggleLibraryButton.setButtonText(iconOnly ? "" : (isLibraryVisible ? "Hide Library" : "Show Library"));

    // Tooltips remain available even in icon-only mode; include shortcut hints where applicable.
    auto hint = [&](const juce::String& base, const juce::String& action) {
        return synth::ui::formatShortcutHint(
            base, ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(action)));
    };

    newButton.setTooltip(hint("New patch", "newPatch"));
    saveButton.setTooltip(hint("Save preset", "savePreset"));
    loadButton.setTooltip(hint("Load preset", "openPreset"));
    settingsButton.setTooltip(hint("Open settings", "openSettings"));
    undoButton.setTooltip(hint("Undo", "undo"));
    redoButton.setTooltip(hint("Redo", "redo"));
    autoArrangeButton.setTooltip(hint("Auto-arrange modules", "autoArrange"));

    const juce::String matrixBase = graphEditor.isModMatrixVisible() ? "Hide Mod Matrix" : "Show Mod Matrix";
    toggleModMatrixButton.setTooltip(hint(matrixBase, "toggleModMatrix"));

    const juce::String aiBase = isAiPanelVisible ? "Hide AI Panel" : "Show AI Panel";
    toggleAiPanelButton.setTooltip(hint(aiBase, "toggleAiPanel"));

    const juce::String libBase = isLibraryVisible ? "Hide Library" : "Show Library";
    toggleLibraryButton.setTooltip(hint(libBase, "toggleLibrary"));
}

// ---- Pure panel-bounds geometry helper ----
MainComponent::PanelBoundsResult MainComponent::computePanelBounds(bool libVisible, bool aiVisible) const {
    int tbH = 36, sbH = 24;
    int libW = libVisible ? 200 : 0;
    int aiW = aiVisible ? 300 : 0;
    if (auto* lf = dynamic_cast<const synth::theme::AppLookAndFeel*>(&getLookAndFeel())) {
        const auto& m = lf->getTheme().metrics;
        tbH = m.toolbarHeight;
        sbH = m.statusBarHeight;
        libW = libVisible ? m.librarySidebarWidth : m.sidebarCollapsedWidth;
        aiW = aiVisible ? m.aiPanelWidth : 0;
    }

    auto bounds = getLocalBounds();
    bounds.removeFromTop(tbH);    // toolbar
    bounds.removeFromBottom(sbH); // status bar

    PanelBoundsResult result;
    if (aiVisible)
        result.aiPanelBounds = bounds.removeFromRight(aiW);
    if (libVisible)
        result.libraryBounds = bounds.removeFromLeft(libW);
    result.graphEditorBounds = bounds;
    return result;
}

// ---- Animated panel transition ----
void MainComponent::animatePanelTransition(const PanelBoundsResult& fromResult, const PanelBoundsResult& toResult,
                                           bool hideLibraryOnComplete, bool hideAiPanelOnComplete) {
    // Snapshot from-bounds for the lambdas.
    libraryAnimFrom = fromResult.libraryBounds.isEmpty() ? toResult.libraryBounds : fromResult.libraryBounds;
    aiPanelAnimFrom = fromResult.aiPanelBounds.isEmpty() ? toResult.aiPanelBounds : fromResult.aiPanelBounds;
    graphEditorAnimFrom = fromResult.graphEditorBounds;

    const auto libTo = toResult.libraryBounds.isEmpty() ? libraryAnimFrom : toResult.libraryBounds;
    const auto aiTo = toResult.aiPanelBounds.isEmpty() ? aiPanelAnimFrom : toResult.aiPanelBounds;
    const auto graphTo = toResult.graphEditorBounds;

    // Stop both animators first — we're doing a single coordinated anim on aiPanelAnim,
    // with libraryAnim as backup for the library bounds.
    libraryAnim.stop(vblankUpdater);
    aiPanelAnim.stop(vblankUpdater);

    // Capture for lambdas.
    auto libFrom = libraryAnimFrom;
    auto aipFrom = aiPanelAnimFrom;
    auto graphFrom = graphEditorAnimFrom;

    // Single animator drives all three panels.
    aiPanelAnim.start(
        vblankUpdater,
        190.0, // ~190 ms — within the 160–220 ms spec
        synth::ui::easeInOutCubic,
        [this, libFrom, libTo, aipFrom, aiTo, graphFrom, graphTo](float t) {
            if (!libFrom.isEmpty() || !libTo.isEmpty())
                moduleLibrary.setBounds(synth::ui::AnimationDriver::lerpBounds(libFrom, libTo, t));
            if (!aipFrom.isEmpty() || !aiTo.isEmpty())
                aiChatComponent.setBounds(synth::ui::AnimationDriver::lerpBounds(aipFrom, aiTo, t));
            graphEditor.setBounds(synth::ui::AnimationDriver::lerpBounds(graphFrom, graphTo, t));
        },
        [this, hideLibraryOnComplete, hideAiPanelOnComplete, libTo, aiTo, graphTo]() {
            // Snap to exact final bounds and apply visibility.
            if (!libTo.isEmpty())
                moduleLibrary.setBounds(libTo);
            if (!aiTo.isEmpty())
                aiChatComponent.setBounds(aiTo);
            graphEditor.setBounds(graphTo);
            if (hideLibraryOnComplete)
                moduleLibrary.setVisible(false);
            if (hideAiPanelOnComplete)
                aiChatComponent.setVisible(false);
        });
}

// ---- Collapsible library sidebar (animated, persisted) ----
void MainComponent::setLibraryVisible(bool v) {
    isLibraryVisible = v;
    appProperties.getUserSettings()->setValue("librarySidebarVisible", v ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // Refresh the toggle button's wide-mode label and tooltip.
    if (!toolbarNarrowMode_)
        toggleLibraryButton.setButtonText(v ? "Hide Library" : "Show Library");
    toggleLibraryButton.setTooltip(synth::ui::formatShortcutHint(
        v ? "Hide Library" : "Show Library",
        ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding("toggleLibrary"))));

    // Compute from/to layouts.
    auto fromResult = computePanelBounds(!v, isAiPanelVisible); // previous layout
    if (v) {
        // Showing: make visible at the from-position before animating.
        moduleLibrary.setVisible(true);
        if (fromResult.libraryBounds.isEmpty())
            fromResult.libraryBounds =
                juce::Rectangle<int>(fromResult.graphEditorBounds.getX(), fromResult.graphEditorBounds.getY(), 0,
                                     fromResult.graphEditorBounds.getHeight());
    }
    auto toResult = computePanelBounds(v, isAiPanelVisible);

    // Apply the FINAL layout immediately so headless tests (no VBlank) see correct bounds.
    // The animation below is cosmetic only — it starts from fromResult and converges to the
    // same toResult that resized() already applied.
    resized();
    if (!v)
        moduleLibrary.setVisible(false);

    animatePanelTransition(fromResult, toResult, /*hideLib=*/!v, /*hideAi=*/false);
}

// ---- Alignment guides toggle (UI Phase 7 - Item 4) ----
void MainComponent::setAlignmentGuidesEnabled(bool enabled) {
    isAlignmentGuidesEnabled = enabled;
    graphEditor.setAlignmentGuidesEnabled(enabled);
}

// ---- Patch name (status bar) ----
void MainComponent::setCurrentPatchName(const juce::String& name) {
    currentPatchName_ = name;
    statusBar.repaint();
}
