#include "../Layout/FadeVisibilityTestGuard.h"
#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AudioEngine/AudioEngine.h"
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
