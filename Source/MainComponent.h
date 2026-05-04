#pragma once

#include "AI/AIIntegrationService.h"
#include "AudioEngine.h"
#include "PresetManager.h"
#include "ShortcutManager.h"
#include "UI/AIChatComponent.h"
#include "UI/GraphEditor.h"
#include "UI/ModuleLibraryComponent.h"
#include "UndoManager.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>

class MainComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::Timer
    , public juce::ApplicationCommandTarget
    , private gsynth::AIIntegrationService::Listener {
public:
    MainComponent(std::unique_ptr<gsynth::AIProvider> provider = nullptr);
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

    // Testing Hooks
    bool isAiPanelConfiguredVisible() const { return isAiPanelVisible; }
    void simulateToggleAiPanelClick() {
        if (toggleAiPanelButton.onClick)
            toggleAiPanelButton.onClick();
    }
    void simulateToggleModMatrixClick() {
        if (toggleModMatrixButton.onClick)
            toggleModMatrixButton.onClick();
    }
    GraphEditor& getGraphEditor() { return graphEditor; }
    void simulateUndoClick() {
        if (undoButton.onClick)
            undoButton.onClick();
    }
    void simulateRedoClick() {
        if (redoButton.onClick)
            redoButton.onClick();
    }
    UndoManager& getUndoManager() { return undoManager; }
    AudioEngine& getAudioEngine() { return audioEngine; }
    void openPresetFromFile();

private:
    // AIIntegrationService::Listener
    void aiPatchApplied() override;

    UndoManager undoManager;
    AudioEngine audioEngine;
    GraphEditor graphEditor;
    ModuleLibraryComponent moduleLibrary;

    juce::TextButton saveButton;
    juce::TextButton loadButton;
    juce::TextButton settingsButton;
    juce::TextButton undoButton;
    juce::TextButton redoButton;
    juce::TextButton toggleAiPanelButton;
    juce::TextButton toggleModMatrixButton;

    std::unique_ptr<juce::FileChooser> fileChooser;

    gsynth::AIIntegrationService aiService;
    gsynth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = true;

    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    float aiPaneWidth = 300.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
