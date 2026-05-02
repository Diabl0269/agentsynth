#include "SettingsWindow.h"
#include "../AI/OllamaProvider.h"
#include "../ShortcutManager.h"

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

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::darkgrey.darker()); }

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
// GeneralSettingsTab - Keyboard shortcuts reference panel
//==============================================================================
class GeneralSettingsTab : public juce::Component {
public:
    GeneralSettingsTab(ShortcutManager& sm)
        : shortcutManager(sm) {
        setWantsKeyboardFocus(true);

        addAndMakeVisible(titleLabel);
        titleLabel.setText("Keyboard Shortcuts", juce::dontSendNotification);
        titleLabel.setFont(juce::FontOptions(18.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colours::white);

        for (auto& actionId : shortcutManager.getActionIds()) {
            actionIds.add(actionId);

            auto descLabel = std::make_unique<juce::Label>();
            descLabel->setText(ShortcutManager::getActionDescription(actionId), juce::dontSendNotification);
            descLabel->setFont(juce::FontOptions(14.0f));
            descLabel->setColour(juce::Label::textColourId, juce::Colours::white);
            addAndMakeVisible(*descLabel);
            descLabels.push_back(std::move(descLabel));

            auto bindButton = std::make_unique<juce::TextButton>();
            bindButton->setButtonText(ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(actionId)));
            bindButton->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
            bindButton->setColour(juce::TextButton::textColourOffId, juce::Colours::white);
            int index = static_cast<int>(bindButtons.size());
            bindButton->onClick = [this, index] { startListening(index); };
            addAndMakeVisible(*bindButton);
            bindButtons.push_back(std::move(bindButton));
        }

        addAndMakeVisible(resetButton);
        resetButton.setButtonText("Reset to Defaults");
        resetButton.setColour(juce::TextButton::buttonColourId, juce::Colour(0xffcc3333));
        resetButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        resetButton.onClick = [this] {
            auto options = juce::MessageBoxOptions()
                               .withIconType(juce::MessageBoxIconType::WarningIcon)
                               .withTitle("Reset Shortcuts")
                               .withMessage("Are you sure you want to reset all keyboard shortcuts to their defaults?")
                               .withButton("Reset")
                               .withButton("Cancel");
            juce::AlertWindow::showAsync(options, [this](int result) {
                if (result == 1) {
                    shortcutManager.resetToDefaults();
                    shortcutManager.saveToProperties();
                    refreshBindingLabels();
                }
            });
        };

        addAndMakeVisible(exportButton);
        exportButton.setButtonText("Export...");
        exportButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        exportButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        exportButton.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Export Shortcuts", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
            auto flags = juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles;
            fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) {
                    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
                    for (auto& actionId : shortcutManager.getActionIds()) {
                        auto binding = shortcutManager.getBinding(actionId);
                        auto value = ShortcutManager::encodeKeyPress(binding);
                        obj->setProperty(actionId, value);
                    }
                    auto json = juce::JSON::toString(juce::var(obj.get()));
                    file.replaceWithText(json);
                }
            });
        };

        addAndMakeVisible(importButton);
        importButton.setButtonText("Import...");
        importButton.setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        importButton.setColour(juce::TextButton::textColourOffId, juce::Colours::white);
        importButton.onClick = [this] {
            fileChooser = std::make_unique<juce::FileChooser>(
                "Import Shortcuts", juce::File::getSpecialLocation(juce::File::userDocumentsDirectory), "*.json");
            auto flags = juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles;
            fileChooser->launchAsync(flags, [this](const juce::FileChooser& fc) {
                auto file = fc.getResult();
                if (file != juce::File{}) {
                    auto json = juce::JSON::parse(file.loadFileAsString());
                    if (auto* obj = json.getDynamicObject()) {
                        for (auto& actionId : shortcutManager.getActionIds()) {
                            if (obj->hasProperty(actionId)) {
                                auto value = obj->getProperty(actionId).toString();
                                auto kp = ShortcutManager::parseKeyPress(value);
                                if (kp.isValid())
                                    shortcutManager.setBinding(actionId, kp);
                            }
                        }
                        shortcutManager.saveToProperties();
                        refreshBindingLabels();
                    }
                }
            });
        };
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colours::darkgrey.darker()); }

    void resized() override {
        auto bounds = getLocalBounds().reduced(15);
        titleLabel.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(10);

        for (size_t i = 0; i < descLabels.size(); ++i) {
            auto row = bounds.removeFromTop(28);
            descLabels[i]->setBounds(row.removeFromLeft(180));
            bindButtons[i]->setBounds(row);
            bounds.removeFromTop(4);
        }

        bounds.removeFromTop(15);
        auto buttonRow = bounds.removeFromTop(28);
        exportButton.setBounds(buttonRow.removeFromLeft(100));
        buttonRow.removeFromLeft(8);
        importButton.setBounds(buttonRow.removeFromLeft(100));
        buttonRow.removeFromLeft(8);
        resetButton.setBounds(buttonRow.removeFromLeft(160));
    }

    bool keyPressed(const juce::KeyPress& key) override {
        if (listeningIndex < 0)
            return false;

        if (key == juce::KeyPress::escapeKey) {
            cancelListening();
            return true;
        }

        // Ignore modifier-only keypresses
        if (key.getKeyCode() == 0)
            return true;

        auto actionId = actionIds[listeningIndex];
        auto conflicting = shortcutManager.getConflictingAction(actionId, key);

        if (conflicting.isNotEmpty()) {
            // Swap: assign the old binding of this action to the conflicting action
            auto oldBinding = shortcutManager.getBinding(actionId);
            shortcutManager.setBinding(conflicting, oldBinding);
        }

        shortcutManager.setBinding(actionId, key);
        shortcutManager.saveToProperties();
        listeningIndex = -1;
        refreshBindingLabels();
        return true;
    }

    int getShortcutCount() const { return static_cast<int>(bindButtons.size()); }

private:
    void startListening(int index) {
        listeningIndex = index;
        bindButtons[static_cast<size_t>(index)]->setButtonText("Press a key...");
        bindButtons[static_cast<size_t>(index)]->setColour(juce::TextButton::buttonColourId, juce::Colours::orange);
        grabKeyboardFocus();
    }

    void cancelListening() {
        if (listeningIndex >= 0) {
            refreshBindingLabels();
            listeningIndex = -1;
        }
    }

    void refreshBindingLabels() {
        for (size_t i = 0; i < bindButtons.size(); ++i) {
            auto actionId = actionIds[static_cast<int>(i)];
            bindButtons[i]->setButtonText(
                ShortcutManager::keyPressToDisplayString(shortcutManager.getBinding(actionId)));
            bindButtons[i]->setColour(juce::TextButton::buttonColourId, juce::Colours::darkgrey);
        }
    }

    ShortcutManager& shortcutManager;
    juce::Label titleLabel;
    juce::StringArray actionIds;
    std::vector<std::unique_ptr<juce::Label>> descLabels;
    std::vector<std::unique_ptr<juce::TextButton>> bindButtons;
    juce::TextButton resetButton;
    juce::TextButton exportButton;
    juce::TextButton importButton;
    std::unique_ptr<juce::FileChooser> fileChooser;
    int listeningIndex = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(GeneralSettingsTab)
};

//==============================================================================
// SettingsWindow - Consolidated tabbed settings interface
//==============================================================================
SettingsWindow::SettingsWindow(juce::AudioDeviceManager& deviceManager, juce::ApplicationProperties& appProperties,
                               gsynth::AIIntegrationService& aiService, gsynth::AIChatComponent& aiChatComponent,
                               ShortcutManager& shortcutManager)
    : appProperties(appProperties) {
    auto* audioSelector = new juce::AudioDeviceSelectorComponent(deviceManager, 0, 2, // min/max inputs
                                                                 0, 2,                // min/max outputs
                                                                 true, true,          // midi
                                                                 false, false         // bit depths
    );
    tabs.addTab("Audio", juce::Colours::darkgrey, audioSelector, true);

    auto* aiSettingsTab = new AISettingsTab(appProperties, aiService, aiChatComponent);
    tabs.addTab("AI", juce::Colours::darkgrey, aiSettingsTab, true);

    auto* generalSettingsTab = new GeneralSettingsTab(shortcutManager);
    tabs.addTab("General", juce::Colours::darkgrey, generalSettingsTab, true);

    addAndMakeVisible(tabs);

    // Restore last selected tab
    tabs.setCurrentTabIndex(appProperties.getUserSettings()->getIntValue("settingsTab", 0), false);
}

SettingsWindow::~SettingsWindow() {
    appProperties.getUserSettings()->setValue("settingsTab", tabs.getCurrentTabIndex());
    appProperties.saveIfNeeded();
}

void SettingsWindow::resized() { tabs.setBounds(getLocalBounds()); }
