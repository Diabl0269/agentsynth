#include "../Layout/FadeVisibilityTestGuard.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AudioEngine/AudioEngine.h"
#include "Branding.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// The AI tab defaults to the hosted server with its address baked in, hides the address box for
// "remote" until a custom server is asked for, and offers one click back to hosted.
class AIHostedServerTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "AIHostedServerTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.folderName = synth::userSettingsRootDirectory().getChildFile("AIHostedServerTest").getFullPathName();
        appProperties.setStorageParameters(options);
        appProperties.getUserSettings()->clear();
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiChatComponent = std::make_unique<synth::AIChatComponent>(*aiService, appProperties);
    }
    void TearDown() override { appProperties.getUserSettings()->clear(); }

    std::unique_ptr<SettingsWindow> openWindow() {
        auto window = std::make_unique<SettingsWindow>(deviceManager, appProperties, *aiService, *aiChatComponent,
                                                       shortcutManager, themeManager, nullptr);
        window->setSize(600, 400);
        window->resized();
        return window;
    }
    template <typename T>
    static T* findByTitle(juce::Component* parent, const juce::String& title) {
        for (auto* child : parent->getChildren())
            if (auto* match = dynamic_cast<T*>(child); match != nullptr && match->getTitle() == title)
                return match;
        return nullptr;
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<synth::AIIntegrationService> aiService;
    std::unique_ptr<synth::AIChatComponent> aiChatComponent;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
    synth::theme::ThemeManager themeManager;
};

TEST_F(AIHostedServerTest, HostedIsTheDefaultAndItsAddressBoxIsHidden) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    auto* hosted = findByTitle<juce::TextButton>(aiTab, "Use the hosted AgentSynth server");
    ASSERT_TRUE(host != nullptr && custom != nullptr && hosted != nullptr);
    EXPECT_FALSE(host->isVisible());
    EXPECT_TRUE(custom->isVisible());
    EXPECT_FALSE(hosted->isVisible());
}

TEST_F(AIHostedServerTest, CustomServerButtonRevealsTheAddressBox) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    ASSERT_TRUE(host != nullptr && custom != nullptr);
    custom->onClick();
    EXPECT_TRUE(host->isVisible());
    EXPECT_FALSE(custom->isVisible());
}

// The way back appears the moment the address box opens, before any address is saved, and one click
// restores the hosted server with an empty address.
TEST_F(AIHostedServerTest, TheWayBackIsThereAsSoonAsTheAddressBoxOpens) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    auto* hosted = findByTitle<juce::TextButton>(aiTab, "Use the hosted AgentSynth server");
    ASSERT_TRUE(host != nullptr && custom != nullptr && hosted != nullptr);
    EXPECT_FALSE(hosted->isVisible());
    custom->onClick();
    host->setText("https://my.example", juce::dontSendNotification); // typed, not yet saved
    EXPECT_TRUE(hosted->isVisible());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "");

    hosted->onClick();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("aiProvider"), "remote");
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "");
    EXPECT_EQ(host->getText(), "");
    EXPECT_FALSE(host->isVisible());
    EXPECT_FALSE(hosted->isVisible());
    EXPECT_TRUE(custom->isVisible());
}

TEST_F(AIHostedServerTest, ASavedCustomServerShowsItsAddressAndOffersTheWayBack) {
    appProperties.getUserSettings()->setValue("aiProvider", "remote");
    appProperties.getUserSettings()->setValue("remoteHost", "https://custom.example");
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* hosted = findByTitle<juce::TextButton>(aiTab, "Use the hosted AgentSynth server");
    ASSERT_TRUE(host != nullptr && hosted != nullptr);
    EXPECT_TRUE(host->isVisible());
    EXPECT_EQ(host->getText(), "https://custom.example");
    EXPECT_TRUE(hosted->isVisible());

    hosted->onClick();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("aiProvider"), "remote");
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "");
    EXPECT_FALSE(host->isVisible());
    EXPECT_FALSE(hosted->isVisible());
}

TEST_F(AIHostedServerTest, UseHostedServerSwitchesAnOllamaUserBackToRemote) {
    appProperties.getUserSettings()->setValue("aiProvider", "ollama");
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* hosted = findByTitle<juce::TextButton>(aiTab, "Use the hosted AgentSynth server");
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    ASSERT_TRUE(hosted != nullptr && host != nullptr);
    EXPECT_TRUE(hosted->isVisible());
    EXPECT_TRUE(host->isVisible()); // Ollama keeps its address box

    hosted->onClick();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("aiProvider"), "remote");
    EXPECT_FALSE(host->isVisible());
    EXPECT_FALSE(hosted->isVisible());
}

// The address box and the two buttons fade in and out in their places instead of popping.
TEST_F(AIHostedServerTest, TheAddressBoxFadesInAndTheCustomButtonFadesOut) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    ASSERT_TRUE(host != nullptr && custom != nullptr);
    ASSERT_FALSE(host->isVisible());
    const auto hostBounds = host->getBounds();

    FadeAnimateGuard guard;
    custom->onClick();
    EXPECT_TRUE(host->isVisible());
    EXPECT_FLOAT_EQ(host->getAlpha(), 0.0f) << "frame 0 of the fade-in";
    EXPECT_TRUE(custom->isVisible()) << "the button stays while it fades out";
    EXPECT_FALSE(interceptsClicks(*custom));

    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(host->getAlpha(), 0.5f, 0.01f);
    EXPECT_NEAR(custom->getAlpha(), 0.5f, 0.01f);

    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(host->getAlpha(), 1.0f);
    EXPECT_FALSE(custom->isVisible());
    EXPECT_EQ(host->getBounds(), hostBounds) << "it fades where it stands; nothing moves";
}

// The hosted server reads "Official" where the address would be; Custom swaps it for the address box and Official
// (use hosted) brings it back with an empty stored address.
TEST_F(AIHostedServerTest, HostedShowsOfficialInPlaceOfTheAddress) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* official = findByTitle<juce::Label>(aiTab, "Server address: Official");
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    auto* hosted = findByTitle<juce::TextButton>(aiTab, "Use the hosted AgentSynth server");
    ASSERT_TRUE(official != nullptr && host != nullptr && custom != nullptr && hosted != nullptr);
    EXPECT_EQ(official->getText(), "Official");
    EXPECT_TRUE(official->isVisible());
    EXPECT_FALSE(official->getTooltip().isEmpty());
    EXPECT_EQ(official->getBounds(), host->getBounds());

    custom->onClick();
    EXPECT_FALSE(official->isVisible());
    EXPECT_TRUE(host->isVisible());
    host->setText("http://localhost:8787", juce::dontSendNotification);
    host->onReturnKey();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "http://localhost:8787");

    hosted->onClick();
    EXPECT_TRUE(official->isVisible());
    EXPECT_FALSE(host->isVisible());
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "");
}

TEST_F(AIHostedServerTest, ASavedCustomServerHidesOfficial) {
    appProperties.getUserSettings()->setValue("aiProvider", "remote");
    appProperties.getUserSettings()->setValue("remoteHost", "https://custom.example");
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* official = findByTitle<juce::Label>(aiTab, "Server address: Official");
    ASSERT_NE(official, nullptr);
    EXPECT_FALSE(official->isVisible());
}

// Typing the official address is the same as choosing Official: nothing is stored, so a later change of the
// default still reaches this install.
TEST_F(AIHostedServerTest, TypingTheOfficialAddressStoresTheDefault) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    auto* official = findByTitle<juce::Label>(aiTab, "Server address: Official");
    ASSERT_TRUE(host != nullptr && custom != nullptr && official != nullptr);
    custom->onClick();
    host->setText(juce::String(synth::branding::kApiBaseUrl) + "/", juce::dontSendNotification);
    host->onReturnKey();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "");
    EXPECT_EQ(host->getText(), "");
    EXPECT_TRUE(official->isVisible());
}

// Plain http://localhost stays allowed for a local test server.
TEST_F(AIHostedServerTest, PlainHttpLocalhostStaysAllowed) {
    auto window = openWindow();
    auto* aiTab = window->getTabs().getTabContentComponent(1);
    auto* host = findByTitle<juce::TextEditor>(aiTab, "AI provider host");
    auto* custom = findByTitle<juce::TextButton>(aiTab, "Use a custom server address");
    ASSERT_TRUE(host != nullptr && custom != nullptr);
    custom->onClick();
    host->setText("http://localhost", juce::dontSendNotification);
    host->onReturnKey();
    EXPECT_EQ(appProperties.getUserSettings()->getValue("remoteHost"), "http://localhost");
}
