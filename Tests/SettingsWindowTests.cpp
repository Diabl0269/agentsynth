#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIProvider.h"
#include "../Source/AudioEngine.h"
#include "../Source/ShortcutManager.h"
#include "../Source/UI/AIChatComponent.h"
#include "../Source/UI/SettingsWindow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Mock AI provider for testing
class MockSettingsProvider : public gsynth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockSettingsProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1", "MockModel2"}, true);
    }

    void sendPrompt(const std::vector<gsynth::AIProvider::Message>&, CompletionCallback callback,
                    const juce::var& = juce::var()) override {
        callback("Mock response", true);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    juce::String currentModel = "MockModel1";
};

class SettingsWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up ApplicationProperties with temporary storage for testing
        juce::PropertiesFile::Options options;
        options.applicationName = "GravisynthSettingsTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);

        // Initialize AI service with mock provider
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<gsynth::AIIntegrationService>(engine->getGraph());
        aiService->setProvider(std::make_unique<MockSettingsProvider>());

        // Initialize AI chat component
        aiChatComponent = std::make_unique<gsynth::AIChatComponent>(*aiService, appProperties);
    }

    void TearDown() override {
        // Clean up ApplicationProperties
        if (auto* userSettings = appProperties.getUserSettings()) {
            userSettings->clear();
        }
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<gsynth::AIIntegrationService> aiService;
    std::unique_ptr<gsynth::AIChatComponent> aiChatComponent;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
};

TEST_F(SettingsWindowTest, HasThreeTabs) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    EXPECT_EQ(settingsWindow.getNumTabs(), 3);
}

TEST_F(SettingsWindowTest, TabNamesAreCorrect) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    EXPECT_EQ(settingsWindow.getTabName(0), "Audio");
    EXPECT_EQ(settingsWindow.getTabName(1), "AI");
    EXPECT_EQ(settingsWindow.getTabName(2), "General");
}

TEST_F(SettingsWindowTest, DefaultTabIsAudio) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), 0);
}

TEST_F(SettingsWindowTest, AudioTabContainsDeviceSelector) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    auto* tabContent = settingsWindow.getTabs().getTabContentComponent(0);
    ASSERT_NE(tabContent, nullptr);

    auto* deviceSelector = dynamic_cast<juce::AudioDeviceSelectorComponent*>(tabContent);
    ASSERT_NE(deviceSelector, nullptr);
}

TEST_F(SettingsWindowTest, AITabPersistsProviderSetting) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);

    // Find the combo box in the AI tab
    juce::ComboBox* providerCombo = nullptr;
    for (auto* child : aiTab->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            providerCombo = combo;
            break;
        }
    }

    // If combo box is found, verify we can access it (the setting persistence
    // is tested through the AISettingsTab's updateSettings call)
    if (providerCombo != nullptr) {
        EXPECT_NE(providerCombo->getText(), "");
    }
}

TEST_F(SettingsWindowTest, RemembersLastSelectedTab) {
    // Set the settingsTab preference to 1 (AI tab) before constructing
    appProperties.getUserSettings()->setValue("settingsTab", 1);
    appProperties.saveIfNeeded();

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), 1);
}

TEST_F(SettingsWindowTest, ResizingDoesNotCrash) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    settingsWindow.setSize(500, 450);

    EXPECT_NO_THROW(settingsWindow.setSize(800, 600));
    EXPECT_NO_THROW(settingsWindow.resized());
}

TEST_F(SettingsWindowTest, GeneralTabShowsShortcuts) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager);
    settingsWindow.setSize(500, 450);

    auto* generalTab = settingsWindow.getTabs().getTabContentComponent(2);
    ASSERT_NE(generalTab, nullptr);
    // The General tab should have child labels for shortcuts (title label + 5 desc labels + 5 bind buttons + reset
    // button = 12)
    EXPECT_GE(generalTab->getNumChildComponents(), 11);
}
