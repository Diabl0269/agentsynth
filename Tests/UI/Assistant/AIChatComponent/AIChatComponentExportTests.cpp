// Export chat: the control exists with its name and tooltip, is disabled while there is nothing to export, and the
// file it writes holds the conversation's turns.
#include "AIChatComponentTestFixture.h"

namespace {

struct ExportRig {
    explicit ExportRig(std::unique_ptr<synth::AIProvider> provider) {
        service.setProvider(std::move(provider));
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatExportTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        historyDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                         .getChildFile("chat-export-" + juce::Uuid().toString());
        chat->setLocalHistoryDirectoryForTesting(historyDir);
        chat->setSize(600, 600);
    }

    void send(const juce::String& text) {
        for (auto* child : chat->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
                if (editor->isVisible())
                    editor->setText(text);
        chat->triggerSend();
        juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    }

    ~ExportRig() { historyDir.deleteRecursively(); }

    juce::File historyDir;
    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    juce::ApplicationProperties props;
    std::unique_ptr<synth::AIChatComponent> chat;
};

} // namespace

TEST_F(AIChatComponentTest, ExportChatControlIsNamedTippedAndDisabledWithNoTurns) {
    ExportRig rig(std::make_unique<MockChatProvider>());
    auto& button = rig.chat->getExportButtonForTesting();
    EXPECT_TRUE(button.isVisible());
    EXPECT_EQ(button.getButtonText(), "Export chat");
    EXPECT_EQ(button.getTitle(), "Export chat");
    EXPECT_FALSE(button.isEnabled());
    EXPECT_TRUE(button.getTooltip().containsIgnoreCase("send a message"));
    EXPECT_TRUE(button.getBounds().getRight() <= rig.chat->getWidth());
    EXPECT_GT(button.getWidth(), 0);
}

TEST_F(AIChatComponentTest, ExportChatEnablesOnceAMessageIsSent) {
    ExportRig rig(std::make_unique<MockChatProvider>());
    rig.send("hello there");
    auto& button = rig.chat->getExportButtonForTesting();
    EXPECT_TRUE(button.isEnabled());
    EXPECT_TRUE(button.getTooltip().containsIgnoreCase("JSON"));
}

TEST_F(AIChatComponentTest, ExportedTextAnswerIsAChatTurnWithItsResponse) {
    ExportRig rig(std::make_unique<MockChatProvider>());
    rig.send("hello there");
    const auto root = rig.chat->buildChatExportForTesting();
    EXPECT_EQ(root["format"].toString(), "agentsynth-chat");
    EXPECT_EQ(root["provider"]["kind"].toString(), "ollama");
    ASSERT_EQ(root["turns"].size(), 1);
    const auto turn = root["turns"][0];
    EXPECT_EQ(turn["request"].toString(), "hello there");
    EXPECT_EQ(turn["kind"].toString(), "chat");
    EXPECT_EQ(turn["response"].toString(), "Mock response text.");
    EXPECT_TRUE((bool)turn["applied"]["ok"]);
}

TEST_F(AIChatComponentTest, ExportedPatchAnswerKeepsTheEnvelopeAndContext) {
    ExportRig rig(std::make_unique<MockPatchProvider>());
    rig.send("Create a fat bass");
    const auto root = rig.chat->buildChatExportForTesting();
    ASSERT_EQ(root["turns"].size(), 1);
    const auto turn = root["turns"][0];
    EXPECT_EQ(turn["kind"].toString(), "patch");
    EXPECT_EQ(turn["envelope"]["nodes"][0]["type"].toString(), "Oscillator");
    EXPECT_TRUE(turn.hasProperty("context"));
    EXPECT_FALSE(turn.hasProperty("response"));
    EXPECT_FALSE((bool)turn["applied"]["ok"]); // the card's Apply has not been pressed
}

TEST_F(AIChatComponentTest, ExportedFailedAnswerRecordsTheError) {
    ExportRig rig(std::make_unique<ErrorProvider>(synth::AIProvider::AIErrorKind::Network, "Error: no connection"));
    rig.send("hello there");
    const auto turn = rig.chat->buildChatExportForTesting()["turns"][0];
    EXPECT_FALSE((bool)turn["applied"]["ok"]);
    EXPECT_EQ(turn["applied"]["error"].toString(), "Error: no connection");
}

TEST_F(AIChatComponentTest, WrittenExportFileParsesBack) {
    ExportRig rig(std::make_unique<MockChatProvider>());
    rig.send("hello there");
    rig.send("and again");
    const auto file = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("chat-export-" + juce::Uuid().toString() + ".json");
    ASSERT_TRUE(rig.chat->writeChatExportForTesting(file));
    const auto root = juce::JSON::parse(file.loadFileAsString());
    file.deleteFile();
    ASSERT_TRUE(root.isObject());
    EXPECT_EQ(root["turns"].size(), 2);
    EXPECT_EQ(root["turns"][1]["request"].toString(), "and again");
}

TEST_F(AIChatComponentTest, RestoredTurnsExportWithTheirTextOnly) {
    ExportRig rig(std::make_unique<MockChatProvider>());
    synth::LocalConversation saved;
    saved.id = "restore-me";
    saved.createdAt = saved.updatedAt = "2026-08-01T00:00:00.000Z";
    saved.title = "Old";
    saved.messages = {{"user", "restored question", saved.createdAt},
                      {"assistant", "restored answer", saved.createdAt}};
    synth::LocalHistoryStore::save(rig.historyDir, saved, synth::LocalHistoryStore::kDefaultRetentionDays);
    rig.chat->simulateRestoreConversationForTesting("restore-me", /*isCloud=*/false);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    EXPECT_TRUE(rig.chat->getExportButtonForTesting().isEnabled());
    const auto root = rig.chat->buildChatExportForTesting();
    ASSERT_EQ(root["turns"].size(), 1);
    const auto turn = root["turns"][0];
    EXPECT_EQ(turn["request"].toString(), "restored question");
    EXPECT_EQ(turn["response"].toString(), "restored answer");
    EXPECT_EQ(turn["kind"].toString(), "chat");
    EXPECT_FALSE(turn.hasProperty("events"));
    EXPECT_FALSE(turn.hasProperty("plan"));
}
