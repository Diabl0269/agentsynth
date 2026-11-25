// AIChatComponentLargeHistoryTests.cpp
// A long history opens as fast as a short one: the history list builds only the rows in view, and a restored chat
// builds bubbles for its newest messages only (older ones when the view reaches the top). The click and key tests go
// through the list's own row path, never straight to restoreConversation().

#include "../../../TestWait.h"
#include "AIChatComponentTestFixture.h"
#include "UI/Assistant/AIChatComponent/ChatHistoryPicker.h"
#include "UI/Assistant/AIChatComponent/SelectableChatText.h"
#include <chrono>
#include <iostream>

namespace {

using synth::ConversationHistorySource;
using synth::LocalConversation;
using synth::LocalConversationSummary;

class BigHistorySource : public ConversationHistorySource {
public:
    std::vector<LocalConversationSummary> conversationsToList;
    LocalConversation conversationToReturn;
    std::vector<juce::String> requestedIds;

    void list(std::function<void(ListResult)> callback) override {
        ListResult result;
        result.ok = true;
        result.conversations = conversationsToList;
        if (callback)
            callback(std::move(result));
    }
    void get(const juce::String& id, std::function<void(bool, LocalConversation)> callback) override {
        requestedIds.push_back(id);
        if (callback)
            callback(true, conversationToReturn);
    }
    void deleteAll(std::function<void(bool, int)> callback) override {
        if (callback)
            callback(true, 0);
    }
};

void configureLargeHistoryProps(juce::ApplicationProperties& props) {
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
}

std::vector<LocalConversationSummary> manyChats(int count) {
    std::vector<LocalConversationSummary> list;
    for (int i = 0; i < count; ++i) {
        LocalConversationSummary s;
        s.id = "chat-" + juce::String(i);
        s.title = "Chat " + juce::String(i);
        s.createdAt = s.updatedAt = "2026-11-20T12:00:00.000Z";
        list.push_back(s);
    }
    return list;
}

LocalConversation longChat(int messageCount) {
    LocalConversation c;
    c.id = "long";
    c.title = "Long";
    c.createdAt = c.updatedAt = "2026-11-20T12:00:00.000Z";
    for (int n = 1; n <= messageCount; ++n)
        c.messages.push_back(
            {n % 2 ? "user" : "assistant", "Message number " + juce::String(n) + " end", "2026-11-20T12:00:00.000Z"});
    return c;
}

int bubblesIn(synth::AIChatComponent& chat) { return chat.getBuiltBubbleCountForTesting(); }

} // namespace

TEST_F(AIChatComponentTest, HistoryListWithAThousandChatsBuildsOnlyTheRowsInView) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    auto local = std::make_unique<BigHistorySource>();
    local->conversationsToList = manyChats(1000);
    chat.setHistorySourcesForTesting(std::move(local), nullptr);

    const auto start = std::chrono::steady_clock::now();
    chat.simulateHistoryButtonClick();
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[FRO2321] history list of 1000 chats opened in " << ms << " ms\n";

    auto* list = chat.historyListForTesting();
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(synth::chat_history::listedCountForTest(*list), 1000);
    EXPECT_LE(synth::chat_history::builtRowCountForTest(*list), 30) << "rows are built for the view, not the list";
    EXPECT_LE(synth::chat_history::heightForTest(*list), 420) << "the list window is capped and scrolls";
    const auto titles = synth::chat_history::visibleTitlesForTest(*list);
    ASSERT_FALSE(titles.empty());
    EXPECT_EQ(titles.front(), "Chat 0");
    EXPECT_EQ(synth::chat_history::visibleDatesForTest(*list).front(), "20 Nov 2026");
}

TEST_F(AIChatComponentTest, HistoryListWithNoChatsSaysSo) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    chat.setHistorySourcesForTesting(std::make_unique<BigHistorySource>(), nullptr);

    chat.simulateHistoryButtonClick();
    auto* list = chat.historyListForTesting();
    ASSERT_NE(list, nullptr);
    const auto titles = synth::chat_history::visibleTitlesForTest(*list);
    ASSERT_EQ(titles.size(), 1u);
    EXPECT_EQ(titles[0], "No saved conversations");
    // The empty-state line is not a chat: choosing it opens nothing.
    synth::chat_history::chooseRowForTest(*list, 0);
    EXPECT_TRUE(synth::test::flushMessageQueue());
    EXPECT_EQ(chat.getMessageCountForTesting(), 0u);
}

TEST_F(AIChatComponentTest, ArrowAndReturnInTheHistoryListOpenThatOneChat) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    auto local = std::make_unique<BigHistorySource>();
    local->conversationsToList = manyChats(1000);
    local->conversationToReturn = longChat(2);
    auto* localPtr = local.get();
    chat.setHistorySourcesForTesting(std::move(local), nullptr);

    chat.simulateHistoryButtonClick();
    auto* list = chat.historyListForTesting();
    ASSERT_NE(list, nullptr);
    EXPECT_TRUE(synth::chat_history::sendKeyForTest(*list, juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(synth::chat_history::sendKeyForTest(*list, juce::KeyPress(juce::KeyPress::downKey)));
    EXPECT_TRUE(synth::chat_history::sendKeyForTest(*list, juce::KeyPress(juce::KeyPress::returnKey)));

    synth::test::waitUntil([&] { return chat.getMessageCountForTesting() >= 2; });
    ASSERT_EQ(localPtr->requestedIds.size(), 1u);
    EXPECT_EQ(localPtr->requestedIds[0], "chat-2") << "Down twice from the first row, Return: the third chat";
    EXPECT_EQ(chat.getMessageCountForTesting(), 2u);
}

TEST_F(AIChatComponentTest, ClickingAHistoryRowOpensThatChatOnly) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    auto local = std::make_unique<BigHistorySource>();
    local->conversationsToList = manyChats(1000);
    local->conversationToReturn = longChat(2);
    auto* localPtr = local.get();
    chat.setHistorySourcesForTesting(std::move(local), nullptr);

    chat.simulateHistoryButtonClick();
    auto* list = chat.historyListForTesting();
    ASSERT_NE(list, nullptr);
    synth::chat_history::chooseRowForTest(*list, 7);

    synth::test::waitUntil([&] { return chat.getMessageCountForTesting() >= 2; });
    ASSERT_EQ(localPtr->requestedIds.size(), 1u) << "one chat is loaded, not the list";
    EXPECT_EQ(localPtr->requestedIds[0], "chat-7");
}

TEST_F(AIChatComponentTest, RestoringAFiveHundredMessageChatBuildsOnlyTheNewestBubbles) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    auto local = std::make_unique<BigHistorySource>();
    local->conversationToReturn = longChat(500);
    chat.setHistorySourcesForTesting(std::move(local), nullptr);

    const auto start = std::chrono::steady_clock::now();
    chat.simulateRestoreConversationForTesting("long", /*isCloud=*/false);
    synth::test::waitUntil([&] { return chat.getMessageCountForTesting() >= 500; });
    const auto ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    std::cout << "[FRO2321] restore of 500 messages took " << ms << " ms, bubbles built: " << bubblesIn(chat) << "\n";

    EXPECT_EQ(chat.getMessageCountForTesting(), 500u) << "the data keeps every turn";
    EXPECT_LE(bubblesIn(chat), 40);
    auto* messageList = findMessageList(chat);
    ASSERT_NE(messageList, nullptr);
    EXPECT_NE(findDescendantWithText<synth::SelectableChatText>(messageList, "Message number 500 end"), nullptr);
    EXPECT_EQ(findDescendantWithText<synth::SelectableChatText>(messageList, "Message number 1 end"), nullptr);
    // The model gets every turn back, whatever is on screen.
    ASSERT_GT(service.getHistory().size(), 2u);
    EXPECT_EQ(service.getHistory().back().content, "Message number 500 end");
}

TEST_F(AIChatComponentTest, ScrollingToTheTopOfARestoredChatBuildsOlderBubblesAndKeepsTheView) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureLargeHistoryProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    auto local = std::make_unique<BigHistorySource>();
    local->conversationToReturn = longChat(500);
    chat.setHistorySourcesForTesting(std::move(local), nullptr);
    chat.simulateRestoreConversationForTesting("long", /*isCloud=*/false);
    synth::test::waitUntil([&] { return chat.getMessageCountForTesting() >= 500; });
    ASSERT_LE(bubblesIn(chat), 40);
    synth::test::flushMessageQueue();
    ASSERT_EQ(chat.getFirstBuiltMessageForTesting(), 460u) << "a restore must not load older messages by itself";

    chat.scrollMessagesToForTesting(0);
    synth::test::waitUntil([&] { return bubblesIn(chat) > 40; });
    EXPECT_EQ(bubblesIn(chat), 80);
    EXPECT_EQ(chat.getFirstBuiltMessageForTesting(), 420u);
    EXPECT_GT(chat.getMessageViewTopForTesting(), 0) << "the view stays on the message it was on, not the new top";

    // Scrolling to the top again, over and over, reaches the very first message.
    for (int i = 0; i < 20 && chat.getFirstBuiltMessageForTesting() > 0; ++i) {
        const auto before = chat.getFirstBuiltMessageForTesting();
        chat.scrollMessagesToForTesting(0);
        synth::test::waitUntil([&] { return chat.getFirstBuiltMessageForTesting() < before; });
    }
    EXPECT_EQ(chat.getFirstBuiltMessageForTesting(), 0u);
    EXPECT_EQ(bubblesIn(chat), 500);
    EXPECT_NE(findDescendantWithText<synth::SelectableChatText>(findMessageList(chat), "Message number 1 end"),
              nullptr);
}
