#pragma once

#include "AI/AIIntegrationService.h"
#include "AudioEngine.h"
#include "GravisynthUndoManager.h"
#include "PresetManager.h"
#include "ShortcutManager.h"
#include "UI/AIChatComponent.h"
#include "UI/GraphEditor.h"
#include "UI/ModuleLibraryComponent.h"
#include "UI/Theme/GravisynthLookAndFeel.h"
#include "UI/Theme/ThemeManager.h"
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

class MainComponent
    : public juce::Component
    , public juce::DragAndDropContainer
    , public juce::Timer
    , public juce::ApplicationCommandTarget
    , private juce::ChangeListener
    , private gsynth::AIIntegrationService::Listener {
public:
    // Primary ctor: receives injected ThemeManager and LookAndFeel from Main.cpp.
    // provider is optional (nullptr → reads saved provider pref from appProperties).
    MainComponent(gsynth::theme::ThemeManager& tm, gsynth::theme::GravisynthLookAndFeel& lf,
                  std::unique_ptr<gsynth::AIProvider> provider = nullptr);

    // Delegating ctor for tests and legacy call sites that don't inject theme objects.
    // Lazily owns private default ThemeManager + GravisynthLookAndFeel instances
    // (stored in ownedThemeManager / ownedLookAndFeel below).
    explicit MainComponent(std::unique_ptr<gsynth::AIProvider> provider = nullptr);

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
    GravisynthUndoManager& getUndoManager() { return undoManager; }
    AudioEngine& getAudioEngine() { return audioEngine; }
    void openPresetFromFile();

private:
    // AIIntegrationService::Listener
    void aiPatchAboutToApply() override;
    void aiPatchApplied() override;

    // ChangeListener (juce::ChangeListener override) — called when ThemeManager broadcasts.
    // Implements the 3-step re-skin pass: applyTheme → sendLookAndFeelChangeMessage → repaint.
    void changeListenerCallback(juce::ChangeBroadcaster* source) override;

    // Shared initialisation body called from both constructors after appProperties is set up.
    void initialiseCommon(std::unique_ptr<gsynth::AIProvider> provider);

    // Owned fallback objects used when the delegating ctor is called (tests/legacy).
    // Null when the primary ctor is used (refs point at external objects instead).
    std::unique_ptr<gsynth::theme::ThemeManager> ownedThemeManager;
    std::unique_ptr<gsynth::theme::GravisynthLookAndFeel> ownedLookAndFeel;

    // Non-owning references to the active ThemeManager and LookAndFeel.
    // Always valid — set by both constructors (either to external objects or to the
    // owned fallbacks above).
    gsynth::theme::ThemeManager* themeManager{nullptr};
    gsynth::theme::GravisynthLookAndFeel* lookAndFeel{nullptr};

    GravisynthUndoManager undoManager;
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
    juce::TextButton autoArrangeButton;

    std::unique_ptr<juce::FileChooser> fileChooser;

    gsynth::AIIntegrationService aiService;
    gsynth::AIChatComponent aiChatComponent;
    bool isAiPanelVisible = false;

    juce::ApplicationProperties appProperties;
    juce::PropertiesFile::Options propertiesOptions;

    ShortcutManager shortcutManager;
    juce::ApplicationCommandManager commandManager;

    float aiPaneWidth = 300.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};
