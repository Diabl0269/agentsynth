// SettingsWindowServerAddressTests.cpp -- the AI tab's server address field refuses an address that is not https
// (bug kind credential-sent-to-unverified-host).

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SettingsWindowServerAddressTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "SettingsServerAddressTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.folderName =
            synth::userSettingsRootDirectory().getChildFile("SettingsServerAddressTest").getFullPathName();
        appProperties.setStorageParameters(options);
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiChatComponent = std::make_unique<synth::AIChatComponent>(*aiService, appProperties);
    }

    void TearDown() override {
        if (auto* userSettings = appProperties.getUserSettings())
            userSettings->clear();
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<synth::AIIntegrationService> aiService;
    std::unique_ptr<synth::AIChatComponent> aiChatComponent;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
    synth::theme::ThemeManager themeManager;
};

// Regression test for FRO2121: a custom server address that is not https (or loopback http) was saved and used. The
// refusal returns before any provider is built, so this runs without a network or a worker thread.
TEST_F(SettingsWindowServerAddressTest, CustomServerAddressThatIsNotHttpsIsRefusedInline) {
    appProperties.getUserSettings()->setValue("aiProvider", "remote");
    appProperties.getUserSettings()->setValue("remoteHost", "https://remote.example");

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();
    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    juce::TextEditor* hostEditor = nullptr;
    for (auto* child : aiTab->getChildren())
        if (hostEditor == nullptr)
            hostEditor = dynamic_cast<juce::TextEditor*>(child);
    ASSERT_NE(hostEditor, nullptr);
    auto* message = dynamic_cast<juce::Label*>(aiTab->findChildWithID("aiHostMessage"));
    ASSERT_NE(message, nullptr);
    EXPECT_FALSE(message->isVisible());

    hostEditor->setText("http://evil.example:8787", juce::dontSendNotification);
    hostEditor->onReturnKey();

    EXPECT_TRUE(message->isVisible());
    EXPECT_TRUE(message->getText().containsIgnoreCase("https"));
    EXPECT_EQ(message->getTitle(), message->getText()) << "the screen reader reads the same text";
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), juce::String("https://remote.example"))
        << "a refused address is not saved";
}
