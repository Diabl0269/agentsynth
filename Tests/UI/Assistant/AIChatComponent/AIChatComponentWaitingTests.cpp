// AIChatComponentWaitingTests.cpp -- onWaitingChanged tells the host when a request goes out (true)
// and when it ends by reply or cancel (false), so the AI toolbar button can show the assistant working.
#include "AIChatComponentTestFixture.h"

namespace {
juce::TextEditor* findInput(synth::AIChatComponent& chat) {
    juce::TextEditor* input = nullptr;
    for (auto* child : chat.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            input = editor;
    return input;
}

void configureProps(juce::ApplicationProperties& props) {
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
}
} // namespace

TEST(AIChatWaitingCallbackTest, FiresTrueOnSendAndFalseOnReply) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto owned = std::make_unique<DeferredPromptProvider>();
    auto* provider = owned.get();
    service.setProvider(std::move(owned));
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    std::vector<bool> seen;
    chat.onWaitingChanged = [&](bool waiting) { seen.push_back(waiting); };

    auto* input = findInput(chat);
    ASSERT_NE(input, nullptr);
    input->setText("hello");
    chat.triggerSend();
    ASSERT_EQ(seen.size(), 1u);
    EXPECT_TRUE(seen[0]);

    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    synth::AIProvider::AIResponse response;
    response.success = true;
    response.content = "Hi there.";
    provider->resolvePrompt(response);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_FALSE(seen[1]);
}

TEST(AIChatWaitingCallbackTest, FiresFalseOnCancelAndOnlyOnce) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    std::vector<bool> seen;
    chat.onWaitingChanged = [&](bool waiting) { seen.push_back(waiting); };

    findInput(chat)->setText("hello");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(20);
    chat.simulateCancelClick();
    chat.simulateEscapeKey(); // nothing is waiting any more: no second call
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_TRUE(seen[0]);
    EXPECT_FALSE(seen[1]);
    EXPECT_FALSE(chat.isWaiting());
}
