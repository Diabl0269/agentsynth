#include "SettingsWindow.h"
#include "../AI/AIProviderRegistry.h"
#include "../ShortcutManager.h"
#include "AppearanceSettingsTab.h"
#include "ShortcutsSettingsTab.h"

//==============================================================================
// AISettingsTab - AI configuration interface
//==============================================================================
class AISettingsTab : public juce::Component {
public:
    AISettingsTab(juce::ApplicationProperties& props, synth::AIIntegrationService& aiServ,
                  synth::AIChatComponent& aiChatComp)
        : appProperties(props)
        , aiService(aiServ)
        , aiChatComponent(aiChatComp)
        , providerRegistry(synth::AIProviderRegistry::createDefault()) {
        addAndMakeVisible(providerLabel);
        providerLabel.setText("AI Provider:", juce::dontSendNotification);

        addAndMakeVisible(providerCombo);
        juce::String savedProviderId = appProperties.getUserSettings()->getValue("aiProvider", "ollama");
        int selectedItemId = 1;
        const auto& allProviders = providerRegistry.listAll();
        for (size_t i = 0; i < allProviders.size(); ++i) {
            int itemId = (int)i + 1;
            providerCombo.addItem(allProviders[i].displayName, itemId);
            if (allProviders[i].id == savedProviderId)
                selectedItemId = itemId;
        }
        providerCombo.setSelectedId(selectedItemId, juce::dontSendNotification);
        providerCombo.onChange = [this] { updateSettings(); };

        addAndMakeVisible(hostLabel);
        addAndMakeVisible(hostEditor);
        hostEditor.setText(appProperties.getUserSettings()->getValue("ollamaHost", "http://localhost:11434"));
        hostEditor.onReturnKey = [this] { updateSettings(); };
        hostEditor.onFocusLost = [this] { updateSettings(); };

        updateHostFieldForSelectedProvider();
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
        const auto* descriptor = selectedDescriptor();
        juce::String providerId = descriptor != nullptr ? descriptor->id : juce::String("ollama");
        juce::String newOllamaHost = hostEditor.getText();

        appProperties.getUserSettings()->setValue("aiProvider", providerId);
        appProperties.getUserSettings()->setValue("ollamaHost", newOllamaHost);
        appProperties.saveIfNeeded();

        updateHostFieldForSelectedProvider();

        aiService.setProvider(providerRegistry.create(providerId, {newOllamaHost, {}}));
        aiChatComponent.refreshModels();
    }

private:
    const synth::ProviderDescriptor* selectedDescriptor() const {
        int selectedIndex = providerCombo.getSelectedItemIndex();
        const auto& all = providerRegistry.listAll();
        if (selectedIndex >= 0 && (size_t)selectedIndex < all.size())
            return &all[(size_t)selectedIndex];
        return nullptr;
    }

    void updateHostFieldForSelectedProvider() {
        const auto* descriptor = selectedDescriptor();
        bool showHost = descriptor == nullptr || descriptor->needsHost;
        hostLabel.setVisible(showHost);
        hostEditor.setVisible(showHost);
        hostLabel.setText(descriptor != nullptr ? (descriptor->displayName + " Host:") : juce::String("Host:"),
                          juce::dontSendNotification);
    }

    juce::ApplicationProperties& appProperties;
    synth::AIIntegrationService& aiService;
    synth::AIChatComponent& aiChatComponent;
    synth::AIProviderRegistry providerRegistry;

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
                               synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                               ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager,
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
