#include "SettingsWindow.h"
#include "../AI/OllamaProvider.h"
#include "../ShortcutManager.h"
#include "AppearanceSettingsTab.h"
#include "ShortcutsSettingsTab.h"

//==============================================================================
// AISettingsTab - AI configuration interface
//==============================================================================
class AISettingsTab : public juce::Component {
public:
    AISettingsTab(juce::ApplicationProperties& props, gsynth::AIIntegrationService& aiServ,
                  gsynth::AIChatComponent& aiChatComp)
        : appProperties(props)
        , aiService(aiServ)
        , aiChatComponent(aiChatComp) {
        addAndMakeVisible(providerLabel);
        providerLabel.setText("AI Provider:", juce::dontSendNotification);

        addAndMakeVisible(providerCombo);
        providerCombo.addItem("Ollama", 1);
        providerCombo.setSelectedId(appProperties.getUserSettings()->getValue("aiProvider", "Ollama") == "Ollama" ? 1
                                                                                                                  : 0,
                                    juce::dontSendNotification);
        providerCombo.onChange = [this] { updateSettings(); };

        addAndMakeVisible(hostLabel);
        hostLabel.setText("Ollama Host:", juce::dontSendNotification);

        addAndMakeVisible(hostEditor);
        hostEditor.setText(appProperties.getUserSettings()->getValue("ollamaHost", "http://localhost:11434"));
        hostEditor.onReturnKey = [this] { updateSettings(); };
        hostEditor.onFocusLost = [this] { updateSettings(); };
    }

    void paint(juce::Graphics& g) override { g.fillAll(findColour(juce::ResizableWindow::backgroundColourId)); }

    void resized() override {
        auto bounds = getLocalBounds().reduced(10);

        auto providerRow = bounds.removeFromTop(25);
        providerLabel.setBounds(providerRow.removeFromLeft(100));
        providerCombo.setBounds(providerRow);

        bounds.removeFromTop(30);
        auto hostRow = bounds.removeFromTop(25);
        hostLabel.setBounds(hostRow.removeFromLeft(100));
        hostEditor.setBounds(hostRow);
    }

    void updateSettings() {
        juce::String selectedProvider = providerCombo.getText();
        juce::String newOllamaHost = hostEditor.getText();

        appProperties.getUserSettings()->setValue("aiProvider", selectedProvider);
        appProperties.getUserSettings()->setValue("ollamaHost", newOllamaHost);
        appProperties.saveIfNeeded();

        // Re-initialize AI service with new provider/host
        if (selectedProvider == "Ollama") {
            aiService.setProvider(std::make_unique<gsynth::OllamaProvider>(newOllamaHost));
        }
        aiChatComponent.refreshModels();
    }

private:
    juce::ApplicationProperties& appProperties;
    gsynth::AIIntegrationService& aiService;
    gsynth::AIChatComponent& aiChatComponent;

    juce::Label providerLabel;
    juce::ComboBox providerCombo;
    juce::Label hostLabel;
    juce::TextEditor hostEditor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsTab)
};

//==============================================================================
// SettingsWindow - Consolidated tabbed settings interface
//==============================================================================
SettingsWindow::SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                               gsynth::AIIntegrationService& aiService, gsynth::AIChatComponent& aiChatComponent,
                               ShortcutManager& shortcutManager, gsynth::theme::ThemeManager& themeManager,
                               GraphEditor* graphEditor)
    : appProperties(appProperties) {
    auto* audioSelector = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, // min/max inputs
                                                                 0, 2,                // min/max outputs
                                                                 true, true,          // midi
                                                                 false, false         // bit depths
    );
    tabs.addTab("Audio", juce::Colours::darkgrey, audioSelector, true);

    auto* aiSettingsTab = new AISettingsTab(appProperties, aiService, aiChatComponent);
    tabs.addTab("AI", juce::Colours::darkgrey, aiSettingsTab, true);

    auto* shortcutsSettingsTab = new ShortcutsSettingsTab(shortcutManager);
    tabs.addTab("Keyboard Shortcuts", juce::Colours::darkgrey, shortcutsSettingsTab, true);

    auto* appearanceSettingsTab = new AppearanceSettingsTab(themeManager, appProperties);
    appearanceSettingsTab->setGraphEditor(graphEditor); // wire the tab to graph editor
    tabs.addTab("Appearance", juce::Colours::darkgrey, appearanceSettingsTab, true);

    addAndMakeVisible(tabs);

    // Restore last selected tab
    tabs.setCurrentTabIndex(appProperties.getUserSettings()->getIntValue("settingsTab", 0), false);
}

SettingsWindow::~SettingsWindow() {
    appProperties.getUserSettings()->setValue("settingsTab", tabs.getCurrentTabIndex());
    appProperties.saveIfNeeded();
}

void SettingsWindow::resized() { tabs.setBounds(getLocalBounds()); }
