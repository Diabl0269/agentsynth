#include "MainComponent.h"
#include "AI/OllamaProvider.h"
#include "UI/SettingsWindow.h"

// ---- Primary constructor (injected ThemeManager + LookAndFeel from Main.cpp) ----
MainComponent::MainComponent(gsynth::theme::ThemeManager& tm, gsynth::theme::GravisynthLookAndFeel& lf,
                             std::unique_ptr<gsynth::AIProvider> provider)
    : graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties)
    , themeManager(&tm)
    , lookAndFeel(&lf) {
    // Setup ApplicationProperties
    propertiesOptions.applicationName = "Gravisynth";
    propertiesOptions.folderName = "Gravisynth";
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

    initialiseCommon(std::move(provider));
}

// ---- Delegating constructor for tests / legacy call sites ----
MainComponent::MainComponent(std::unique_ptr<gsynth::AIProvider> provider)
    : graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService, appProperties) {
    // Own a default ThemeManager + LookAndFeel so the code behaves identically
    // to the primary-ctor path (no special-casing in the rest of the class).
    ownedThemeManager = std::make_unique<gsynth::theme::ThemeManager>();
    ownedLookAndFeel = std::make_unique<gsynth::theme::GravisynthLookAndFeel>();
    themeManager = ownedThemeManager.get();
    lookAndFeel = ownedLookAndFeel.get();

    // Setup ApplicationProperties (same as primary ctor)
    propertiesOptions.applicationName = "Gravisynth";
    propertiesOptions.folderName = "Gravisynth";
    propertiesOptions.filenameSuffix = "settings";
    propertiesOptions.osxLibrarySubFolder = "Application Support";
    propertiesOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Initialise theme with appProperties so the persisted theme is restored.
    themeManager->initialise(&appProperties);
    lookAndFeel->applyTheme(themeManager->getActiveTheme());
    themeManager->addChangeListener(this);

    initialiseCommon(std::move(provider));
}

// ---- Shared post-construction body ----
void MainComponent::initialiseCommon(std::unique_ptr<gsynth::AIProvider> provider) {
    // ORDERING CONTRACT: read the persisted panel-visibility flags FIRST, before any
    // setVisible()/addAndMakeVisible() call that depends on them. These override the member
    // initialisers (isLibraryVisible{true}, isAiPanelVisible=false).
    isLibraryVisible = appProperties.getUserSettings()->getBoolValue("librarySidebarVisible", true);
    isAiPanelVisible = appProperties.getUserSettings()->getBoolValue("aiPanelVisible", false);

    // Load AI provider preference
    juce::String savedProviderName = appProperties.getUserSettings()->getValue("aiProvider", "Ollama");
    juce::String savedOllamaHost = appProperties.getUserSettings()->getValue("ollamaHost", "http://localhost:11434");

    if (provider) {
        aiService.setProvider(std::move(provider));
    } else if (savedProviderName == "Ollama") {
        aiService.setProvider(std::make_unique<gsynth::OllamaProvider>(savedOllamaHost));
    }

    // NOTE: Do NOT call aiChatComponent.refreshModels() here. The AIChatComponent
    // constructor already kicks off model discovery on construction. A second call
    // would trigger a redundant /api/tags request on startup.
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
    addAndMakeVisible(saveButton);
    saveButton.setComponentID("saveButton");
    saveButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                graphEditor.savePreset(file);
                setCurrentPatchName(file.getFileNameWithoutExtension());
            }
        });
    };

    addAndMakeVisible(loadButton);
    loadButton.setComponentID("loadButton");
    loadButton.onClick = [this] {
        juce::PopupMenu menu;
        auto presets = gsynth::PresetManager::getPresetList();
        auto categories = gsynth::PresetManager::getCategories();
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
                // loadFactoryPreset detaches existing components (stopping scope timers) before the graph
                // is cleared — avoids a use-after-free where a ScopeComponent reads a freed VisualBuffer.
                graphEditor.loadFactoryPreset(result - 1);
                setCurrentPatchName(presets[result - 1].name);
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
        aiChatComponent.setVisible(isAiPanelVisible);
        // Persist BEFORE resized() so a crash during layout doesn't lose the user's choice.
        appProperties.getUserSettings()->setValue("aiPanelVisible", isAiPanelVisible ? "1" : "0");
        appProperties.getUserSettings()->saveIfNeeded();
        applyToolbarIcons();
        resized();
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
                                                aiChatComponent, shortcutManager, *themeManager);
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
    toolbar.setButtons({&toggleLibraryButton, &saveButton, &loadButton, &settingsButton, &undoButton, &redoButton,
                        &autoArrangeButton, &toggleModMatrixButton, &toggleAiPanelButton});

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
    auto presets = gsynth::PresetManager::getPresetList();
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
            graphEditor.loadPreset(file);
            setCurrentPatchName(file.getFileNameWithoutExtension());
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
    commands.addArray({GravisynthCommands::openSettings, GravisynthCommands::savePreset, GravisynthCommands::openPreset,
                       GravisynthCommands::undo, GravisynthCommands::redo, GravisynthCommands::toggleModMatrix,
                       GravisynthCommands::toggleAiPanel, GravisynthCommands::autoArrange,
                       GravisynthCommands::toggleLibrary});
}

void MainComponent::getCommandInfo(juce::CommandID commandID, juce::ApplicationCommandInfo& result) {
    switch (commandID) {
    case GravisynthCommands::openSettings: {
        result.setInfo("Open Settings", "Open the settings window", "General", 0);
        auto kp = shortcutManager.getBinding("openSettings");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::savePreset: {
        result.setInfo("Save Preset", "Save the current preset", "General", 0);
        auto kp = shortcutManager.getBinding("savePreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::openPreset: {
        result.setInfo("Open Preset", "Open a preset file", "General", 0);
        auto kp = shortcutManager.getBinding("openPreset");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::undo: {
        result.setInfo("Undo", "Undo the last action", "Edit", 0);
        auto kp = shortcutManager.getBinding("undo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::redo: {
        result.setInfo("Redo", "Redo the last undone action", "Edit", 0);
        auto kp = shortcutManager.getBinding("redo");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::toggleModMatrix: {
        result.setInfo("Toggle Mod Matrix", "Toggle the modulation matrix panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleModMatrix");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::toggleAiPanel: {
        result.setInfo("Toggle AI Panel", "Toggle the AI chat panel", "View", 0);
        auto kp = shortcutManager.getBinding("toggleAiPanel");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::autoArrange: {
        result.setInfo("Auto Arrange", "Auto-arrange modules by signal flow", "View", 0);
        auto kp = shortcutManager.getBinding("autoArrange");
        result.addDefaultKeypress(kp.getKeyCode(), kp.getModifiers());
        break;
    }
    case GravisynthCommands::toggleLibrary: {
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
    case GravisynthCommands::openSettings:
        if (settingsButton.onClick)
            settingsButton.onClick();
        return true;
    case GravisynthCommands::savePreset:
        if (saveButton.onClick)
            saveButton.onClick();
        return true;
    case GravisynthCommands::openPreset:
        openPresetFromFile();
        return true;
    case GravisynthCommands::undo:
        if (undoManager.canUndo())
            undoManager.undo();
        return true;
    case GravisynthCommands::redo:
        if (undoManager.canRedo())
            undoManager.redo();
        return true;
    case GravisynthCommands::toggleModMatrix:
        toggleModMatrixButton.triggerClick();
        return true;
    case GravisynthCommands::toggleAiPanel:
        toggleAiPanelButton.triggerClick();
        return true;
    case GravisynthCommands::autoArrange:
        graphEditor.autoArrange();
        return true;
    case GravisynthCommands::toggleLibrary:
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
    auto cmdId = GravisynthCommands::getCommandForAction(action);
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
    if (auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel())) {
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
    using gsynth::theme::Icon;

    auto* lf = dynamic_cast<gsynth::theme::GravisynthLookAndFeel*>(&getLookAndFeel());

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

    // Tooltips remain available even in icon-only mode.
    toggleLibraryButton.setTooltip(isLibraryVisible ? "Hide Library" : "Show Library");
    toggleAiPanelButton.setTooltip(isAiPanelVisible ? "Hide AI Panel" : "Show AI Panel");
    toggleModMatrixButton.setTooltip(graphEditor.isModMatrixVisible() ? "Hide Mod Matrix" : "Show Mod Matrix");
}

// ---- Collapsible library sidebar (persisted) ----
void MainComponent::setLibraryVisible(bool v) {
    isLibraryVisible = v;
    moduleLibrary.setVisible(v);
    toggleLibraryButton.setTooltip(v ? "Hide Library" : "Show Library");
    appProperties.getUserSettings()->setValue("librarySidebarVisible", v ? "1" : "0");
    appProperties.getUserSettings()->saveIfNeeded();
    // Refresh the toggle button's wide-mode label, then re-lay-out.
    if (!toolbarNarrowMode_)
        toggleLibraryButton.setButtonText(v ? "Hide Library" : "Show Library");
    resized();
}

// ---- Patch name (status bar) ----
void MainComponent::setCurrentPatchName(const juce::String& name) {
    currentPatchName_ = name;
    statusBar.repaint();
}
