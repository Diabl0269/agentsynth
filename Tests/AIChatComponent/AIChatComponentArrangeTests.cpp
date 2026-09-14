// AIChatComponentArrangeTests.cpp
// Arrange mode: selector gating on the timeline preference regardless of provider, explicit
// routing to capability vs. prompt transport, and the validated/rejected timeline-card response
// flow. Shared mocks and the AIChatComponentTest fixture live in AIChatComponentTestFixture.h.
//
// Core component tests live in AIChatComponentTests.cpp; history/upsell/rating-sync tests live in
// AIChatComponentHistoryTests.cpp.

#include "AIChatComponentTestFixture.h"

// ============================================================================
// Arrange mode: selector gating, explicit routing, and the timeline card flow.
// ============================================================================

namespace {

// Provider (hosted by default, local when `hosted` is flipped) that answers
// sendCapabilityRequest() with a canned timelineOps envelope and counts which entry point each
// request used — the seam the routing tests observe.
class ArrangeCapableProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "ArrangeCapableProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        ++sendPromptCalls;
        AIResponse response;
        response.success = true;
        response.content = promptResponse;
        callback(response);
        return {};
    }

    RequestId sendCapabilityRequest(const juce::String& capability, const juce::var&,
                                    CompletionCallback callback) override {
        ++capabilityCalls;
        lastCapability = capability;
        AIResponse response;
        response.success = true;
        response.content = cannedEnvelope;
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }
    bool isHosted() const override { return hosted; }

    bool hosted = true; // flip to false to stand in for a local (Ollama-shaped) provider
    int sendPromptCalls = 0;
    int capabilityCalls = 0;
    juce::String lastCapability;
    juce::String promptResponse = "plain answer";
    juce::String cannedEnvelope = R"({"timelineOps":[{"op":"addTrack","kind":"midi","name":"Bass"}]})";

private:
    juce::String currentModel;
    int requestTimeoutMs = 240000;
};

// The recurring ApplicationProperties boilerplate, in one place for the arrange tests.
struct TestAppProperties {
    juce::ApplicationProperties props;
    TestAppProperties() {
        juce::PropertiesFile::Options options;
        options.applicationName = "Test";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        props.setStorageParameters(options);
    }
};

// Same child walk SendMessageUpdatesUIAndHistory uses to reach the input field.
juce::TextEditor* findChatInputField(juce::Component& parent) {
    juce::TextEditor* found = nullptr;
    for (auto* child : parent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            found = editor;
    return found;
}

} // namespace

TEST_F(AIChatComponentTest, ArrangeModeSelectorFollowsTimelinePreferenceRegardlessOfProvider) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    TestAppProperties props;
    synth::AIChatComponent chat(service, props.props);
    chat.setSize(400, 600);

    // Timeline preference off: hidden, whatever the provider.
    service.setProvider(std::make_unique<MockChatProvider>());
    chat.refreshModels();
    EXPECT_FALSE(chat.isModeSelectorVisibleForTesting());

    // LOCAL provider + timeline preference on + live context: visible — the parity rule; arrange
    // mode is served on both transports, so the provider never gates the UI.
    synth::TimelineDoc doc;
    synth::TransportService transport;
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);
    chat.refreshModeControls();
    EXPECT_TRUE(chat.isModeSelectorVisibleForTesting());

    // Provider switch to hosted: still visible, nothing about the gate changed.
    service.setProvider(std::make_unique<HostedMockProvider>());
    chat.refreshModels(); // the post-setProvider resync point (CLAUDE.md ordering contract)
    EXPECT_TRUE(chat.isModeSelectorVisibleForTesting());

    // Switch toggled off mid-session (the owner re-syncs the selector after flipping it): hidden
    // again.
    service.setTimelineToolsEnabled(false);
    chat.refreshModeControls();
    EXPECT_FALSE(chat.isModeSelectorVisibleForTesting());

    // And back on.
    service.setTimelineToolsEnabled(true);
    chat.refreshModeControls();
    EXPECT_TRUE(chat.isModeSelectorVisibleForTesting());
}

TEST_F(AIChatComponentTest, ArrangeModeWithLocalProviderRoutesToPromptTransportAndShowsCard) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    synth::TimelineDoc doc;
    synth::TransportService transport;
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);

    ArrangeCapableProvider* provider = nullptr;
    {
        auto p = std::make_unique<ArrangeCapableProvider>();
        provider = p.get();
        provider->hosted = false;
        provider->promptResponse = provider->cannedEnvelope; // what a grammar-constrained local model returns
        service.setProvider(std::move(p));
    }

    TestAppProperties props;
    synth::AIChatComponent chat(service, props.props);
    chat.setSize(400, 600);
    chat.refreshModels();
    ASSERT_TRUE(chat.isModeSelectorVisibleForTesting()) << "the selector shows for a local provider too";
    chat.setArrangeModeForTesting(true);

    auto* input = findChatInputField(chat);
    ASSERT_NE(input, nullptr);
    input->setText("add a bass track");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // Local transport: sendPrompt, never the capability endpoint — and the downstream card flow
    // is identical to the hosted case (transport-agnostic by construction).
    EXPECT_EQ(provider->sendPromptCalls, 1);
    EXPECT_EQ(provider->capabilityCalls, 0);
    EXPECT_TRUE(chat.getLastTimelineOpsJsonForTesting().isNotEmpty());
    EXPECT_TRUE(chat.getLastTimelineOpsPreviewForTesting().contains("Bass"));
}

TEST_F(AIChatComponentTest, ArrangeModeRoutesToCapabilityAndPatchModeToPrompt) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    synth::TimelineDoc doc;
    synth::TransportService transport;
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);

    ArrangeCapableProvider* provider = nullptr;
    {
        auto p = std::make_unique<ArrangeCapableProvider>();
        provider = p.get();
        service.setProvider(std::move(p));
    }

    TestAppProperties props;
    synth::AIChatComponent chat(service, props.props);
    chat.setSize(400, 600);
    chat.refreshModels();
    ASSERT_TRUE(chat.isModeSelectorVisibleForTesting());

    auto* input = findChatInputField(chat);
    ASSERT_NE(input, nullptr);

    // Arrange selected: the capability endpoint — even though the text names a module and would
    // classify as patch-related, because routing is the selector's call alone (no keyword guessing).
    chat.setArrangeModeForTesting(true);
    input->setText("add a filter sweep");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    EXPECT_EQ(provider->capabilityCalls, 1);
    EXPECT_EQ(provider->sendPromptCalls, 0);
    EXPECT_EQ(provider->lastCapability, juce::String("timeline.generate"));

    // Patch selected: the conversation path — even for arrangement-sounding text.
    chat.setArrangeModeForTesting(false);
    input->setText("arrange a song on the timeline");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);
    EXPECT_EQ(provider->capabilityCalls, 1);
    EXPECT_EQ(provider->sendPromptCalls, 1);
}

TEST_F(AIChatComponentTest, ArrangeResponseShowsValidatedTimelineCard) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    synth::TimelineDoc doc;
    synth::TransportService transport;
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);

    ArrangeCapableProvider* provider = nullptr;
    {
        auto p = std::make_unique<ArrangeCapableProvider>();
        provider = p.get();
        service.setProvider(std::move(p));
    }
    juce::ignoreUnused(provider);

    TestAppProperties props;
    synth::AIChatComponent chat(service, props.props);
    chat.setSize(400, 600);
    chat.refreshModels();
    ASSERT_TRUE(chat.isModeSelectorVisibleForTesting());
    chat.setArrangeModeForTesting(true);

    auto* input = findChatInputField(chat);
    ASSERT_NE(input, nullptr);
    input->setText("add a bass track");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // The canned envelope validates against the live doc, so the card offers Apply (json kept)
    // and the preview is the validator's own summary of what an apply would do.
    EXPECT_TRUE(chat.getLastTimelineOpsJsonForTesting().isNotEmpty());
    EXPECT_TRUE(chat.getLastTimelineOpsPreviewForTesting().contains("Bass"));
}

TEST_F(AIChatComponentTest, ArrangeResponseFailingValidationShowsRejectionWithoutApply) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    synth::TimelineDoc doc;
    synth::TransportService transport;
    service.setTimelineContext(&doc, &transport);
    service.setTimelineToolsEnabled(true);

    ArrangeCapableProvider* provider = nullptr;
    {
        auto p = std::make_unique<ArrangeCapableProvider>();
        provider = p.get();
        service.setProvider(std::move(p));
    }
    // An op TimelineOps::validate refuses (unknown track kind). The server's own repair-retry
    // already ran; the client shows the rejection in the card and offers NO retry loop.
    provider->cannedEnvelope = R"({"timelineOps":[{"op":"addTrack","kind":"bogus","name":"X"}]})";

    TestAppProperties props;
    synth::AIChatComponent chat(service, props.props);
    chat.setSize(400, 600);
    chat.refreshModels();
    ASSERT_TRUE(chat.isModeSelectorVisibleForTesting());
    chat.setArrangeModeForTesting(true);

    auto* input = findChatInputField(chat);
    ASSERT_NE(input, nullptr);
    input->setText("add a weird track");
    chat.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // Shown, never swallowed — but with nothing appliable behind it.
    EXPECT_TRUE(chat.getLastTimelineOpsJsonForTesting().isEmpty());
    EXPECT_TRUE(chat.getLastTimelineOpsPreviewForTesting().contains("NOT applied"));
}
