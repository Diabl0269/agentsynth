#include "SettingsWindow.h"
#include "../AI/AIProviderRegistry.h"
#include "../Branding.h"
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
        // P4-6: hosted mode sends the prompt and current patch off this machine to Agent Synth's
        // servers; local (Ollama) mode never leaves it. See also the same disclosure next to the
        // model picker in AIChatComponent, which most users see far more often than this dialog.
        providerCombo.setTooltip("Local (Ollama) processing stays on this machine. Hosted mode "
                                 "sends your prompt and current patch to Agent Synth's servers "
                                 "for processing \xe2\x80\x94 see " +
                                 juce::String(synth::branding::kWebsiteUrl) + "/privacy.");

        // Built once and reused by selectedDescriptor() below: populating the combo from a
        // filtered view but then indexing selectedDescriptor() into the UNFILTERED
        // providerRegistry.listAll() would desync the moment a hidden provider sits between two
        // visible ones (combo index N would no longer be listAll()[N]). visibleProviders is the
        // single source of truth for both.
        for (const auto& descriptor : providerRegistry.listAll())
            if (!descriptor.hidden)
                visibleProviders.push_back(&descriptor);

        juce::String savedProviderId = appProperties.getUserSettings()->getValue("aiProvider", "remote");
        int selectedItemId = 1;
        for (size_t i = 0; i < visibleProviders.size(); ++i) {
            int itemId = (int)i + 1;
            providerCombo.addItem(visibleProviders[i]->displayName, itemId);
            if (visibleProviders[i]->id == savedProviderId)
                selectedItemId = itemId;
        }
        providerCombo.setSelectedId(selectedItemId, juce::dontSendNotification);
        providerCombo.onChange = [this] {
            // Each provider persists its own host under its own key (see hostSettingsKeyFor()) —
            // swap the field to the NEWLY selected provider's own saved value BEFORE
            // updateSettings() persists anything, or the previous provider's host text gets
            // written under the new provider's key.
            const auto* descriptor = selectedDescriptor();
            hostEditor.setText(
                appProperties.getUserSettings()->getValue(hostSettingsKeyFor(descriptor), defaultHostFor(descriptor)),
                juce::dontSendNotification);
            updateSettings();
        };

        addAndMakeVisible(hostLabel);
        addAndMakeVisible(hostEditor);
        {
            const auto* initialDescriptor = selectedDescriptor();
            hostEditor.setText(appProperties.getUserSettings()->getValue(hostSettingsKeyFor(initialDescriptor),
                                                                         defaultHostFor(initialDescriptor)));
        }
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
        juce::String hostKey = hostSettingsKeyFor(descriptor);
        juce::String newHost = hostEditor.getText();

        appProperties.getUserSettings()->setValue("aiProvider", providerId);
        appProperties.getUserSettings()->setValue(hostKey, newHost);
        appProperties.saveIfNeeded();

        updateHostFieldForSelectedProvider();

        aiService.setProvider(providerRegistry.create(providerId, {newHost, {}}));
        aiChatComponent.refreshModels();
    }

private:
    const synth::ProviderDescriptor* selectedDescriptor() const {
        int selectedIndex = providerCombo.getSelectedItemIndex();
        if (selectedIndex >= 0 && (size_t)selectedIndex < visibleProviders.size())
            return visibleProviders[(size_t)selectedIndex];
        return nullptr;
    }

    // Each provider persists its own host under its own settings key — sharing one key (the
    // pre-P4-6 behaviour) meant switching providers silently pointed the new one at whatever host
    // string the previous provider had left behind (e.g. RemoteProvider constructed against
    // Ollama's port). Mirrors MainComponent::initialiseCommon()'s equivalent lookup.
    static juce::String hostSettingsKeyFor(const synth::ProviderDescriptor* descriptor) {
        return (descriptor != nullptr && descriptor->id == "remote") ? "remoteHost" : "ollamaHost";
    }

    // Empty for "remote" so an unset value falls through to AIProviderRegistry::createDefault()'s
    // synth::branding::kApiBaseUrl fallback, rather than duplicating that URL here.
    static juce::String defaultHostFor(const synth::ProviderDescriptor* descriptor) {
        return (descriptor != nullptr && descriptor->id == "remote") ? juce::String()
                                                                     : juce::String("http://localhost:11434");
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
    // Non-owning pointers into providerRegistry's storage — valid for the AISettingsTab's
    // lifetime because providerRegistry is a fellow member and outlives this vector. See the
    // constructor for why this must be the ONLY thing the combo population and
    // selectedDescriptor() index into.
    std::vector<const synth::ProviderDescriptor*> visibleProviders;

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
    : appProperties(appProperties)
    , themeManager(themeManager) {
    auto* audioSelector = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, // min/max inputs
                                                                 0, 2,                // min/max outputs
                                                                 true, true,          // midi
                                                                 false, false         // bit depths
    );
    tabs.addTab("Audio", juce::Colours::transparentBlack, audioSelector, true);

    auto* aiSettingsTab = new AISettingsTab(appProperties, aiService, aiChatComponent);
    tabs.addTab("AI", juce::Colours::transparentBlack, aiSettingsTab, true);

    auto* shortcutsSettingsTab = new ShortcutsSettingsTab(shortcutManager);
    tabs.addTab("Keyboard Shortcuts", juce::Colours::transparentBlack, shortcutsSettingsTab, true);

    auto* appearanceSettingsTab = new AppearanceSettingsTab(themeManager, appProperties);
    appearanceSettingsTab->setGraphEditor(graphEditor); // wire the tab to graph editor
    tabs.addTab("Appearance", juce::Colours::transparentBlack, appearanceSettingsTab, true);

    addAndMakeVisible(tabs);

    // Restore last selected tab
    tabs.setCurrentTabIndex(appProperties.getUserSettings()->getIntValue("settingsTab", 0), false);

    themeManager.addChangeListener(this);
}

SettingsWindow::~SettingsWindow() {
    themeManager.removeChangeListener(this);
    appProperties.getUserSettings()->setValue("settingsTab", tabs.getCurrentTabIndex());
    appProperties.saveIfNeeded();
}

void SettingsWindow::resized() { tabs.setBounds(getLocalBounds()); }

void SettingsWindow::changeListenerCallback(juce::ChangeBroadcaster* /*source*/) {
    sendLookAndFeelChange();
    repaint();
}
