// AIChatAccessibilityTests.cpp -- what a screen reader hears from the AI chat: the message wording
// builder (Source/UI/Assistant/ChatMessageAccessibilityText.h), the message list being a Tab-stop
// list, each message bubble reading as one list item, and the input row's names and tooltips.
#include "AIChatComponentTestFixture.h"
#include "UI/Assistant/ChatMessageAccessibilityText.h"

using synth::ui::describeChatMessageForAccessibility;

TEST(ChatMessageAccessibilityTextTest, NamesTheSpeaker) {
    EXPECT_EQ(describeChatMessageForAccessibility("user", "make it wetter", false, false), "You: make it wetter");
    EXPECT_EQ(describeChatMessageForAccessibility("assistant", "Done", false, false), "Assistant: Done");
    EXPECT_EQ(describeChatMessageForAccessibility("system", "Hi", false, false), "Assistant: Hi");
}

TEST(ChatMessageAccessibilityTextTest, MentionsWhatTheMessageCanApply) {
    EXPECT_EQ(describeChatMessageForAccessibility("assistant", "Done", true, false),
              "Assistant: Done. Includes a patch you can apply");
    EXPECT_EQ(describeChatMessageForAccessibility("assistant", "Done", false, true),
              "Assistant: Done. Includes timeline changes you can apply");
}

namespace {

struct ChatRig {
    ChatRig() {
        service.setProvider(std::make_unique<MockChatProvider>());
        juce::PropertiesFile::Options options;
        options.applicationName = "AIChatAccessibilityTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
        chat = std::make_unique<synth::AIChatComponent>(service, props);
        chat->setLocalHistoryDirectoryForTesting(juce::File::getSpecialLocation(juce::File::tempDirectory)
                                                     .getChildFile("chat-a11y-" + juce::Uuid().toString()));
        chat->setSize(400, 600);
    }

    juce::Viewport* viewport() {
        for (auto* child : chat->getChildren())
            if (auto* v = dynamic_cast<juce::Viewport*>(child))
                return v;
        return nullptr;
    }

    AudioEngine engine;
    synth::AIIntegrationService service{engine.getGraph()};
    juce::ApplicationProperties props;
    std::unique_ptr<synth::AIChatComponent> chat;
};

} // namespace

TEST_F(AIChatComponentTest, MessageListIsANamedTabStopList) {
    ChatRig rig;
    auto* viewport = rig.viewport();
    ASSERT_NE(viewport, nullptr);
    EXPECT_TRUE(viewport->getWantsKeyboardFocus());
    EXPECT_EQ(viewport->getTitle(), "Chat messages");

    const auto handler = viewport->createAccessibilityHandler();
    ASSERT_NE(handler, nullptr);
    EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::list);
}

TEST_F(AIChatComponentTest, EachMessageIsAListItemThatReadsItsText) {
    ChatRig rig;
    for (auto* child : rig.chat->getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            if (editor->isVisible())
                editor->setText("Create a fat bass");
    rig.chat->triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* list = findMessageList(*rig.chat);
    ASSERT_NE(list, nullptr);
    int bubbles = 0;
    bool sawUserMessage = false;
    for (auto* child : list->getChildren()) {
        if (child->getTitle().isEmpty())
            continue;
        ++bubbles;
        const auto handler = child->createAccessibilityHandler();
        ASSERT_NE(handler, nullptr);
        EXPECT_EQ(handler->getRole(), juce::AccessibilityRole::listItem);
        sawUserMessage = sawUserMessage || child->getTitle() == "You: Create a fat bass";
    }
    EXPECT_GE(bubbles, 1);
    EXPECT_TRUE(sawUserMessage);
}

TEST_F(AIChatComponentTest, InputRowControlsHaveNamesAndTooltips) {
    ChatRig rig;
    int checked = 0;
    for (auto* child : rig.chat->getChildren()) {
        if (!child->isVisible())
            continue;
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
            EXPECT_TRUE(editor->getTitle().isNotEmpty());
            EXPECT_TRUE(editor->getTooltip().isNotEmpty());
            ++checked;
        } else if (auto* button = dynamic_cast<juce::Button*>(child)) {
            EXPECT_TRUE(button->getButtonText().isNotEmpty() || button->getTitle().isNotEmpty());
            EXPECT_TRUE(button->getTooltip().isNotEmpty()) << button->getButtonText();
            ++checked;
        }
    }
    EXPECT_GE(checked, 3);
}
