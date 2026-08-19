#include "SettingsWindow.h"
#include "../AI/AIProviderRegistry.h"
#include "../AI/LocalHistoryStore.h"
#include "../Branding.h"
#include "../ShortcutManager.h"
#include "AppearanceSettingsTab.h"
#include "PreferencesSettingsTab.h"
#include "ShortcutsSettingsTab.h"

//==============================================================================
// AISettingsTab - AI configuration interface
//==============================================================================
class AISettingsTab : public juce::Component {
public:
    AISettingsTab(juce::ApplicationProperties& props, synth::AIIntegrationService& aiServ,
                  synth::AIChatComponent& aiChatComp, synth::AccountService* accountServiceIn)
        : appProperties(props)
        , aiService(aiServ)
        , aiChatComponent(aiChatComp)
        , accountService(accountServiceIn)
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

        // P6-8: local chat-history retention. This is deliberately the only *local* retention
        // control — cloud retention stays the server's single global default (see docs/AI_Engine.md).
        // Added AFTER providerCombo/hostEditor above so existing tests that grab "the first
        // ComboBox"/"the first TextEditor" in this tab keep resolving to the provider controls.
        addAndMakeVisible(historyRetentionLabel);
        historyRetentionLabel.setText("Local History:", juce::dontSendNotification);

        addAndMakeVisible(historyRetentionCombo);
        historyRetentionCombo.setTooltip("How long AI chat history stays on this device. Independent of "
                                         "cloud sync, which subscribers get automatically.");
        historyRetentionCombo.addItem("30 days", kRetention30Id);
        historyRetentionCombo.addItem("90 days", kRetention90Id);
        historyRetentionCombo.addItem("180 days", kRetention180Id);
        historyRetentionCombo.addItem("365 days", kRetention365Id);
        historyRetentionCombo.addItem("Keep forever", kRetentionForeverId);

        const int savedRetentionDays = appProperties.getUserSettings()->getIntValue(
            "historyRetentionDays", synth::LocalHistoryStore::kDefaultRetentionDays);
        historyRetentionCombo.setSelectedId(itemIdForRetentionDays(savedRetentionDays), juce::dontSendNotification);
        historyRetentionCombo.onChange = [this] {
            appProperties.getUserSettings()->setValue("historyRetentionDays",
                                                      retentionDaysForItemId(historyRetentionCombo.getSelectedId()));
            appProperties.saveIfNeeded();
        };

        // P6-7: opt-in prompt collection for product learning. Human review only — never used to
        // train/fine-tune models (see docs/AI_Engine.md, and the "we do not use your prompts to
        // train AI models" promise in the privacy policy this deliberately doesn't touch).
        // Disabled + "sign in required" tooltip when signed out, same gating precedent as
        // AccountRow/PlanBadge reading AccountService's published state.
        addAndMakeVisible(promptLearningToggle);
        promptLearningToggle.setButtonText("Help improve AgentSynth \xe2\x80\x94 share my hosted-mode "
                                           "prompts for product learning");
        promptLearningToggle.onClick = [this] {
            if (accountService != nullptr)
                accountService->setPromptLearningOptIn(promptLearningToggle.getToggleState());
        };

        if (accountService != nullptr) {
            // Chains onto whatever onStateChanged already holds (AIChatComponent installs it once
            // at startup and owns the slot for the app's lifetime — see its setAccountService()) —
            // AccountService::onStateChanged is a single std::function slot, not a multicast
            // delegate, so overwriting it outright would silently stop AIChatComponent's
            // accountRow/planBadge from refreshing for as long as this dialog stays open.
            // Restored verbatim in the destructor: safe only because nothing else touches
            // onStateChanged while a SettingsWindow is open (AIChatComponent's own installation
            // happens exactly once, at MainComponent construction).
            juce::Component::SafePointer<AISettingsTab> safeThis(this);
            previousOnStateChanged = accountService->onStateChanged;
            accountService->onStateChanged = [safeThis, previous = previousOnStateChanged] {
                if (previous)
                    previous();
                if (auto* self = safeThis.getComponent())
                    self->refreshPromptLearningFromAccountService();
            };

            // Reflect the last-known snapshot immediately (synchronous, matches
            // AccountRow::setAccountService()'s contract), then kick a fresh fetch so the checkbox
            // shows the server's actual current value rather than a possibly-stale cached one.
            refreshPromptLearningFromAccountService();
            if (accountService->getSnapshot().state == synth::AccountState::SignedIn)
                accountService->refreshPromptLearningOptIn();
        } else {
            refreshPromptLearningFromAccountService();
        }
    }

    ~AISettingsTab() override {
        if (accountService != nullptr)
            accountService->onStateChanged = previousOnStateChanged;
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

        bounds.removeFromTop(30);
        auto historyRow = bounds.removeFromTop(25);
        historyRetentionLabel.setBounds(historyRow.removeFromLeft(100));
        historyRetentionCombo.setBounds(historyRow.removeFromLeft(150));

        bounds.removeFromTop(30);
        promptLearningToggle.setBounds(bounds.removeFromTop(40));
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
    // Reflects AccountService's current published snapshot onto promptLearningToggle: enabled +
    // checked-per-server when signed in, disabled + unchecked + "sign in required" tooltip
    // otherwise (a signed-out snapshot's promptLearningOptIn is always false anyway — see
    // AccountSnapshot's doc comment — but the explicit unchecked-when-disabled below doesn't rely
    // on that, since accountService itself may be null in tests).
    void refreshPromptLearningFromAccountService() {
        const bool signedIn =
            accountService != nullptr && accountService->getSnapshot().state == synth::AccountState::SignedIn;
        promptLearningToggle.setEnabled(signedIn);
        promptLearningToggle.setTooltip(signedIn ? juce::String() : juce::String("Sign in required"));
        promptLearningToggle.setToggleState(signedIn && accountService->getSnapshot().promptLearningOptIn,
                                            juce::dontSendNotification);
    }

    // ComboBox item ids for the retention control. Deliberately NOT the raw day counts (a
    // ComboBox item id of 0 is reserved by JUCE for "no selection"), so kRetentionForeverId maps
    // to LocalHistoryStore::kRetainForever (-1) via retentionDaysForItemId() below rather than
    // being stored directly.
    static constexpr int kRetention30Id = 1;
    static constexpr int kRetention90Id = 2;
    static constexpr int kRetention180Id = 3;
    static constexpr int kRetention365Id = 4;
    static constexpr int kRetentionForeverId = 5;

    static int retentionDaysForItemId(int itemId) {
        switch (itemId) {
        case kRetention30Id:
            return 30;
        case kRetention90Id:
            return 90;
        case kRetention365Id:
            return 365;
        case kRetentionForeverId:
            return synth::LocalHistoryStore::kRetainForever;
        case kRetention180Id:
        default:
            return 180;
        }
    }

    static int itemIdForRetentionDays(int days) {
        switch (days) {
        case 30:
            return kRetention30Id;
        case 90:
            return kRetention90Id;
        case 365:
            return kRetention365Id;
        case synth::LocalHistoryStore::kRetainForever:
            return kRetentionForeverId;
        case 180:
        default:
            return kRetention180Id; // out-of-range persisted value falls back to the 180-day default
        }
    }

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
    // Nullable: nullptr in every context that doesn't wire an account (e.g. most existing tests),
    // in which case promptLearningToggle just stays permanently disabled — see
    // refreshPromptLearningFromAccountService().
    synth::AccountService* accountService = nullptr;
    // Captured/restored around accountService->onStateChanged — see the constructor/destructor.
    std::function<void()> previousOnStateChanged;
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
    juce::Label historyRetentionLabel;
    juce::ComboBox historyRetentionCombo;
    juce::ToggleButton promptLearningToggle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AISettingsTab)
};

//==============================================================================
// SettingsWindow - Consolidated tabbed settings interface
//==============================================================================
SettingsWindow::SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                               synth::AIIntegrationService& aiService, synth::AIChatComponent& aiChatComponent,
                               ShortcutManager& shortcutManager, synth::theme::ThemeManager& themeManager,
                               GraphEditor* graphEditor, synth::AccountService* accountService, bool showAudioTab)
    : appProperties(appProperties)
    , themeManager(themeManager) {
    if (showAudioTab) {
        auto* audioSelector = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, // min/max inputs
                                                                     0, 2,                // min/max outputs
                                                                     true, true,          // midi
                                                                     false, false         // bit depths
        );
        tabs.addTab("Audio", juce::Colours::transparentBlack, audioSelector, true);
    }

    auto* aiSettingsTab = new AISettingsTab(appProperties, aiService, aiChatComponent, accountService);
    tabs.addTab("AI", juce::Colours::transparentBlack, aiSettingsTab, true);

    auto* shortcutsSettingsTab = new ShortcutsSettingsTab(shortcutManager);
    tabs.addTab("Keyboard Shortcuts", juce::Colours::transparentBlack, shortcutsSettingsTab, true);

    auto* preferencesSettingsTab = new PreferencesSettingsTab(appProperties);
    preferencesSettingsTab->setGraphEditor(graphEditor);
    tabs.addTab("Preferences", juce::Colours::transparentBlack, preferencesSettingsTab, true);

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
