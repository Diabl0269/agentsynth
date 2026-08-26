#include "../Source/UI/FeedbackSettingsTab.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

class FeedbackSettingsTabTest : public ::testing::Test {
protected:
    void SetUp() override {
        tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("FeedbackSettingsTabTest_" + juce::Uuid().toString())
                       .getChildFile("general_feedback.jsonl");
    }

    void TearDown() override { tempFile.getParentDirectory().deleteRecursively(); }

    juce::StringArray lines() const {
        juce::StringArray result;
        result.addLines(tempFile.loadFileAsString());
        result.removeEmptyStrings();
        return result;
    }

    juce::File tempFile;
};

TEST_F(FeedbackSettingsTabTest, SendButtonStartsDisabled) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, TypingTextEnablesSendButton) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("something broke", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_TRUE(tab.getSendButtonForTest().isEnabled());

    tab.getFeedbackEditorForTest().setText("", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, WhitespaceOnlyTextDoesNotEnableSendButton) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("   \n  ", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, ClickingSendRecordsToStoreAndClearsEditor) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("please add MIDI export", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getCategoryComboForTest().setSelectedId(2, juce::sendNotificationSync); // Feature Request
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    EXPECT_TRUE(tab.getFeedbackEditorForTest().getText().isEmpty());
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
    EXPECT_TRUE(tab.getStatusTextForTest().isNotEmpty());

    ASSERT_EQ(lines().size(), 1);
    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getProperty("text").toString(), "please add MIDI export");
    EXPECT_EQ(entry->getProperty("category").toString(), "feature");
}

TEST_F(FeedbackSettingsTabTest, EditingAfterSendClearsStatusMessage) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("first note", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    ASSERT_TRUE(tab.getStatusTextForTest().isNotEmpty());

    tab.getFeedbackEditorForTest().setText("second note", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_TRUE(tab.getStatusTextForTest().isEmpty());
}

TEST_F(FeedbackSettingsTabTest, ResizingDoesNotCrash) {
    FeedbackSettingsTab tab;
    tab.setSize(500, 450);
    EXPECT_NO_THROW(tab.setSize(800, 600));
    EXPECT_NO_THROW(tab.resized());
}
