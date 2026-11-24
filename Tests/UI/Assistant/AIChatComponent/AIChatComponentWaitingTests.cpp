// AIChatComponentWaitingTests.cpp -- onWaitingChanged tells the host when a request goes out (true)
// and when it ends by reply or cancel (false), so the AI toolbar button can show the assistant working.
#include "../../../TestWait.h"
#include "../../Layout/FadeVisibilityTestGuard.h"
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

    synth::test::flushMessageQueue();
    synth::AIProvider::AIResponse response;
    response.success = true;
    response.content = "Hi there.";
    provider->resolvePrompt(response);
    synth::test::waitUntil([&] { return (seen.size()) == (2u); });
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
    synth::test::flushMessageQueue();
    chat.simulateCancelClick();
    chat.simulateEscapeKey(); // nothing is waiting any more: no second call
    ASSERT_EQ(seen.size(), 2u);
    EXPECT_TRUE(seen[0]);
    EXPECT_FALSE(seen[1]);
    EXPECT_FALSE(chat.isWaiting());
}

// Cancel and the spinner fade in with a request and out when it ends, and are hidden only after the fade.
TEST(AIChatWaitingFadeTest, CancelAndSpinnerFadeInAndOut) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    juce::TextButton* cancel = nullptr;
    for (auto* child : chat.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b != nullptr && b->getButtonText() == "Cancel")
            cancel = b;
    ASSERT_NE(cancel, nullptr);

    FadeAnimateGuard guard;
    findInput(chat)->setText("hello");
    chat.triggerSend();
    EXPECT_TRUE(cancel->isVisible());
    EXPECT_FLOAT_EQ(cancel->getAlpha(), 0.0f) << "fades in, no pop";
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(cancel->getAlpha(), 1.0f);
    EXPECT_TRUE(chat.isCancelVisible());

    chat.simulateCancelClick();
    EXPECT_FALSE(chat.isCancelVisible()) << "logically gone at once";
    EXPECT_TRUE(cancel->isVisible()) << "but on screen until the fade ends";
    synth::ui::FadeVisibility::stepAllForTest(0.5f);
    EXPECT_NEAR(cancel->getAlpha(), 0.5f, 0.01f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FALSE(cancel->isVisible());
}

TEST(AIChatWaitingFadeTest, CancelHidesAtOnceWhenAnimationsAreOff) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    findInput(chat)->setText("hello");
    chat.triggerSend();
    EXPECT_TRUE(chat.isCancelVisible());
    chat.simulateCancelClick();
    EXPECT_FALSE(chat.isCancelVisible());
    for (auto* child : chat.getChildren())
        if (auto* b = dynamic_cast<juce::TextButton*>(child); b != nullptr && b->getButtonText() == "Cancel") {
            EXPECT_FALSE(b->isVisible());
            EXPECT_FLOAT_EQ(b->getAlpha(), 1.0f);
        }
}

namespace {
juce::TextButton* findSend(synth::AIChatComponent& chat) {
    for (auto* child : chat.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child);
            button != nullptr && button->getButtonText() == "Send")
            return button;
    return nullptr;
}
} // namespace

TEST(AIChatProjectLoadingTest, SendWaitsForAProjectToFinishLoading) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    auto* send = findSend(chat);
    auto* input = findInput(chat);
    ASSERT_NE(send, nullptr);
    ASSERT_NE(input, nullptr);
    const auto normalTip = send->getTooltip();
    ASSERT_TRUE(send->isEnabled());

    int sent = 0;
    chat.onMessageSent = [&] { ++sent; };
    chat.setProjectLoading(true);
    EXPECT_FALSE(send->isEnabled());
    EXPECT_EQ(send->getTooltip(), "Available once the project has finished loading");
    EXPECT_FALSE(send->getButtonText().isEmpty()) << "keeps its title for screen readers";
    input->setText("hello");
    chat.triggerSend(); // what Enter in the input does
    EXPECT_EQ(sent, 0);
    EXPECT_EQ(input->getText(), "hello") << "the typed message is kept";
    EXPECT_FALSE(chat.isWaiting());

    chat.setProjectLoading(false);
    EXPECT_TRUE(send->isEnabled());
    EXPECT_EQ(send->getTooltip(), normalTip);
    chat.triggerSend();
    EXPECT_EQ(sent, 1);
    EXPECT_TRUE(chat.isWaiting());
    EXPECT_FALSE(send->isEnabled()) << "disabled while the request is out";
    chat.setProjectLoading(true);
    chat.setProjectLoading(false);
    EXPECT_FALSE(send->isEnabled()) << "a finished load does not re-enable Send behind a request in flight";
}

// While the assistant thinks, the chat itself shows a spinner beside the "AI is thinking" line (not only the toolbar
// button and the input row), fades it in, and drops it when the reply arrives.
TEST(AIChatWaitingFadeTest, TheChatShowsASpinnerBesideTheThinkingLineUntilTheReplyArrives) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto owned = std::make_unique<DeferredPromptProvider>();
    auto* provider = owned.get();
    service.setProvider(std::move(owned));
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);
    EXPECT_EQ(chat.getWaitingSpinnerForTesting(), nullptr) << "nothing spins before a request";

    FadeAnimateGuard guard;
    findInput(chat)->setText("hello");
    chat.triggerSend();
    auto* spinner = chat.getWaitingSpinnerForTesting();
    auto* line = chat.getWaitingLabelForTesting();
    ASSERT_NE(spinner, nullptr);
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(spinner->getParentComponent(), line->getParentComponent()) << "inside the message list";
    EXPECT_TRUE(spinner->isVisible());
    EXPECT_FLOAT_EQ(spinner->getAlpha(), 0.0f) << "fades in, no pop";
    EXPECT_FLOAT_EQ(line->getAlpha(), 0.0f);
    synth::ui::FadeVisibility::stepAllForTest(1.0f);
    EXPECT_FLOAT_EQ(spinner->getAlpha(), 1.0f);
    EXPECT_LE(spinner->getRight(), line->getX()) << "the dot sits left of the line";
    EXPECT_GE(spinner->getY(), line->getY());
    EXPECT_LE(spinner->getBottom(), line->getBottom()) << "on the line's own row";

    synth::test::flushMessageQueue();
    synth::AIProvider::AIResponse response;
    response.success = true;
    response.content = "Hi there.";
    provider->resolvePrompt(response);
    synth::test::waitUntil([&] { return !chat.isWaiting(); });
    EXPECT_EQ(chat.getWaitingSpinnerForTesting(), nullptr) << "the reply replaces the thinking row";
}

// The thinking line stays on screen and its time counts up while the provider stays silent (no reply, no timeout).
TEST(AIChatWaitingFadeTest, TheThinkingLineKeepsShowingAndItsTimerCountsUpWhileTheProviderIsSilent) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    FadeAnimateGuard guard;
    findInput(chat)->setText("hello");
    chat.triggerSend();
    auto* line = chat.getWaitingLabelForTesting();
    ASSERT_NE(line, nullptr);
    EXPECT_EQ(line->getComponentID(), "chatThinkingLabel");
    EXPECT_TRUE(chat.getWaitingStatusText().startsWith("AI is thinking... "));
    const auto first = chat.getWaitingStatusText();

    synth::test::waitUntil([&] { return chat.getWaitingStatusText() != first; });
    EXPECT_NE(chat.getWaitingStatusText(), first) << "the timer moved on while the provider said nothing";
    EXPECT_TRUE(chat.getWaitingStatusText().startsWith("AI is thinking... "));
    EXPECT_TRUE(chat.isWaiting());
    ASSERT_NE(chat.getWaitingSpinnerForTesting(), nullptr) << "dot and line stay until the reply";
    EXPECT_TRUE(chat.getWaitingLabelForTesting()->isVisible());
    EXPECT_TRUE(chat.getWaitingSpinnerForTesting()->isVisible());
}

// Regression test for FRO2195: the thinking line had no accessible name, so a screen reader and the test mode's control
// list never met it. Its title is the line itself, elapsed time included, and follows the time.
TEST(AIChatWaitingFadeTest, TheThinkingLineHasAnAccessibleNameThatFollowsItsTimer) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    FadeAnimateGuard guard;
    findInput(chat)->setText("hello");
    chat.triggerSend();
    auto* line = chat.getWaitingLabelForTesting();
    ASSERT_NE(line, nullptr);
    EXPECT_TRUE(line->getTitle().startsWith("AI is thinking... "));
    EXPECT_EQ(line->getTitle(), chat.getWaitingStatusText());
    const auto first = line->getTitle();

    synth::test::waitUntil([&] { return chat.getWaitingStatusText() != first; });
    EXPECT_EQ(line->getTitle(), chat.getWaitingStatusText()) << "the name moves on with the time";
    EXPECT_NE(line->getTitle(), first);
}

// Regression test for FRO2196: Cancel sat over a Send that was still drawn, so "Send" showed through the "Cancel"
// label.
TEST(AIChatWaitingFadeTest, SendIsOutOfTheWayWhileCancelTakesItsPlace) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    auto* send = findSend(chat);
    ASSERT_NE(send, nullptr);
    EXPECT_TRUE(send->isVisible());
    findInput(chat)->setText("hello");
    chat.triggerSend();
    EXPECT_FALSE(send->isVisible()) << "Cancel takes Send's place";
    chat.simulateCancelClick();
    EXPECT_TRUE(send->isVisible()) << "Send is back once the request is over";
}

// With animations Off the dot is held at full strength (it does not pulse), so the proof still shows it.
TEST(AIChatWaitingFadeTest, UnderAnimationsOffTheThinkingDotStaysSteadyAtFullStrength) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<DeferredPromptProvider>());
    juce::ApplicationProperties props;
    configureProps(props);
    synth::AIChatComponent chat(service, props);
    chat.setSize(400, 600);

    FadeAnimateGuard guard(synth::ui::AnimationMode::off);
    findInput(chat)->setText("hello");
    chat.triggerSend();
    auto* dot = chat.getWaitingSpinnerForTesting();
    ASSERT_NE(dot, nullptr);
    auto centreAlpha = [&] {
        const auto image = dot->createComponentSnapshot(dot->getLocalBounds());
        return image.getPixelAt(image.getWidth() / 2, image.getHeight() / 2).getAlpha();
    };
    const auto before = centreAlpha();
    synth::test::waitUntil([] { return false; }, 700); // outlasts a pulse half-cycle; nothing to poll for
    EXPECT_EQ(centreAlpha(), before);
    EXPECT_EQ(before, 255);
}
