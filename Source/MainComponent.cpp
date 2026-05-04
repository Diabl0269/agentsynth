#include "MainComponent.h"
#include "AI/OllamaProvider.h"
#include "UI/SettingsWindow.h"

MainComponent::MainComponent(std::unique_ptr<gsynth::AIProvider> provider)
    : graphEditor(audioEngine, &undoManager)
    , aiService(audioEngine.getGraph())
    , aiChatComponent(aiService) {
    // Setup ApplicationProperties
    propertiesOptions.applicationName = "Gravisynth";
    propertiesOptions.folderName = "Gravisynth";
    propertiesOptions.filenameSuffix = "settings";
    propertiesOptions.osxLibrarySubFolder = "Application Support";
    propertiesOptions.storageFormat = juce::PropertiesFile::storeAsXML;
    appProperties.setStorageParameters(propertiesOptions);
    shortcutManager.loadFromProperties(appProperties);

    // Load AI provider preference
    juce::String savedProviderName = appProperties.getUserSettings()->getValue("aiProvider", "Ollama");
    juce::String savedOllamaHost = appProperties.getUserSettings()->getValue("ollamaHost", "http://localhost:11434");

    if (provider) {
        aiService.setProvider(std::move(provider));
    } else if (savedProviderName == "Ollama") {
        aiService.setProvider(std::make_unique<gsynth::OllamaProvider>(savedOllamaHost));
    }

    aiChatComponent.refreshModels();

    aiService.addListener(this);
    setSize(1600, 900);
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
    graphEditor.getModMatrix().setVisible(graphEditor.isModMatrixVisible());

    // Buttons
    addAndMakeVisible(saveButton);
    saveButton.setButtonText("Save Preset");
    saveButton.setComponentID("saveButton");
    saveButton.onClick = [this] {
        fileChooser = std::make_unique<juce::FileChooser>(
            "Save Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
        auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
        fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
            auto file = fc.getResult();
            if (file != juce::File{}) {
                graphEditor.savePreset(file);
            }
        });
    };

    addAndMakeVisible(loadButton);
    loadButton.setButtonText("Load Presets");
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
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&loadButton), [this](int result) {
            if (result == 1000) {
                openPresetFromFile();
            } else if (result > 0) {
                if (gsynth::PresetManager::loadPreset(result - 1, audioEngine.getGraph())) {
                    graphEditor.updateComponents();
                }
            }
        });
    };

    addAndMakeVisible(undoButton);
    undoButton.setButtonText("Undo");
    undoButton.setComponentID("undoButton");
    undoButton.setEnabled(false);
    undoButton.onClick = [this] {
        if (undoManager.canUndo())
            undoManager.undo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(redoButton);
    redoButton.setButtonText("Redo");
    redoButton.setComponentID("redoButton");
    redoButton.setEnabled(false);
    redoButton.onClick = [this] {
        if (undoManager.canRedo())
            undoManager.redo();
        undoButton.setEnabled(undoManager.canUndo());
        redoButton.setEnabled(undoManager.canRedo());
    };

    addAndMakeVisible(toggleAiPanelButton);
    toggleAiPanelButton.setButtonText("Hide AI");
    toggleAiPanelButton.setComponentID("toggleAiPanel");
    toggleAiPanelButton.onClick = [this] {
        isAiPanelVisible = !isAiPanelVisible;
        aiChatComponent.setVisible(isAiPanelVisible);
        toggleAiPanelButton.setButtonText(isAiPanelVisible ? "Hide AI" : "Show AI");
        resized();
    };

    addAndMakeVisible(toggleModMatrixButton);
    toggleModMatrixButton.setButtonText(graphEditor.isModMatrixVisible() ? "Hide Matrix" : "Show Matrix");
    toggleModMatrixButton.setComponentID("toggleModMatrix");
    toggleModMatrixButton.onClick = [this] {
        graphEditor.toggleModMatrixVisibility();
        toggleModMatrixButton.setButtonText(graphEditor.isModMatrixVisible() ? "Hide Matrix" : "Show Matrix");
        resized();
    };

    addAndMakeVisible(settingsButton);
    settingsButton.setButtonText("Settings");
    settingsButton.setComponentID("settingsButton");
    settingsButton.onClick = [this]() {
        auto* settingsComp = new SettingsWindow(audioEngine.getDeviceManager(), appProperties, aiService,
                                                aiChatComponent, shortcutManager);
        settingsComp->setSize(500, 450);

        juce::DialogWindow::LaunchOptions options;
        options.content.setOwned(settingsComp);
        options.dialogTitle = "Settings";
        options.componentToCentreAround = this;
        options.useNativeTitleBar = true;
        options.resizable = true;
        options.launchAsync();
    };

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
    stopTimer();
    aiService.removeListener(this);
    graphEditor.detachAllModuleComponents();
    audioEngine.shutdown();
}

void MainComponent::timerCallback() {
    undoButton.setEnabled(undoManager.canUndo());
    redoButton.setEnabled(undoManager.canRedo());
}

void MainComponent::aiPatchApplied() {
    juce::Component::SafePointer<MainComponent> safeThis(this);
    juce::MessageManager::callAsync([safeThis]() {
        if (auto* self = safeThis.getComponent())
            self->graphEditor.updateComponents();
    });
}

void MainComponent::openPresetFromFile() {
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load Preset", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
    auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
    fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
        auto file = fc.getResult();
        if (file != juce::File{}) {
            graphEditor.loadPreset(file);
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
                       GravisynthCommands::toggleAiPanel});
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
    auto bounds = getLocalBounds();
    auto header = bounds.removeFromTop(30);

    saveButton.setBounds(header.removeFromLeft(100).reduced(2));
    loadButton.setBounds(header.removeFromLeft(120).reduced(2));
    settingsButton.setBounds(header.removeFromLeft(100).reduced(2));
    undoButton.setBounds(header.removeFromLeft(80).reduced(2));
    redoButton.setBounds(header.removeFromLeft(80).reduced(2));

    // Position toggle buttons on the right side of the header
    toggleAiPanelButton.setBounds(header.removeFromRight(100).reduced(2));
    toggleModMatrixButton.setBounds(header.removeFromRight(100).reduced(2));

    if (isAiPanelVisible) {
        aiChatComponent.setBounds(bounds.removeFromRight((int)aiPaneWidth));
    }

    moduleLibrary.setBounds(bounds.removeFromLeft(200));
    graphEditor.setBounds(bounds);
}
