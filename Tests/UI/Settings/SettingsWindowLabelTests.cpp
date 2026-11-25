// SettingsWindowLabelTests.cpp -- the AI tab's row labels read in full for every provider (bug kind layout-overlap).

#include "AI/AIIntegrationService/AIIntegrationService.h"
#include "AudioEngine/AudioEngine.h"
#include "ShortcutManager/ShortcutManager.h"
#include "UI/Assistant/AIChatComponent/AIChatComponent.h"
#include "UI/Settings/SettingsWindow.h"
#include "UI/Theme/ThemeManager.h"
#include "UserSettings.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class SettingsWindowLabelTest : public ::testing::Test {
protected:
    void SetUp() override {
        juce::PropertiesFile::Options options;
        options.applicationName = "SettingsLabelTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        options.folderName = synth::userSettingsRootDirectory().getChildFile("SettingsLabelTest").getFullPathName();
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

// layout-overlap: the address label was built from the provider name ("Remote (hosted) Host:"), wider than its
// column, so JUCE elided it. Each visible provider's AI-tab labels must fit their bounds at the default size, and
// the address label says "Server address:" whatever the provider is.
TEST_F(SettingsWindowLabelTest, EveryAiTabLabelReadsInFullForEachProvider) {
    for (const juce::String providerId : {"ollama", "remote"}) {
        appProperties.getUserSettings()->setValue("aiProvider", providerId);
        // Remote hides the address box until it is revealed; a saved custom address opens it.
        appProperties.getUserSettings()->setValue("remoteHost", "https://remote.example");
        SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                      themeManager, nullptr);
        settingsWindow.setSize(600, 400);
        settingsWindow.resized();
        auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
        ASSERT_NE(aiTab, nullptr);

        int labels = 0;
        bool sawAddressLabel = false;
        for (auto* child : aiTab->getChildren()) {
            auto* label = dynamic_cast<juce::Label*>(child);
            // Only the left-column labels (the message line and captions span the width).
            if (label == nullptr || label->getText().isEmpty() || label->getX() > 20 || label->getWidth() > 250)
                continue;
            ++labels;
            const auto needed = juce::GlyphArrangement::getStringWidth(label->getFont(), label->getText());
            EXPECT_LE(needed, static_cast<float>(label->getWidth()))
                << providerId << ": \"" << label->getText() << "\" is wider than its " << label->getWidth()
                << " px box";
            if (label->getText().startsWith("Server address"))
                sawAddressLabel = true;
        }
        EXPECT_GE(labels, 3) << providerId;
        EXPECT_TRUE(sawAddressLabel) << providerId << ": the address label should read \"Server address:\"";
    }
}
