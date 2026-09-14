// HTTP-status/body -> AIErrorKind mapping, and the requestId every response must carry.
#include "OllamaProviderTestFixture.h"

// REGRESSION LOCK: httpStatus 401/403 must map to AIErrorKind::Auth so the UI can
// distinguish "sign in again" from other failure modes.
TEST_F(OllamaProviderTest, MapsUnauthorizedToAuthError) {
    auto factory = [](const juce::URL&, const juce::URL::InputStreamOptions& options,
                      const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        if (auto* statusCode = options.getStatusCode())
            *statusCode = 401;
        return nullptr;
    };

    synth::OllamaProvider provider{"http://mock-host:11434", factory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Auth);
}

// REGRESSION LOCK: httpStatus 429 must map to AIErrorKind::RateLimit and parse a
// Retry-After header when the server provides one.
TEST_F(OllamaProviderTest, MapsTooManyRequestsToRateLimit) {
    auto factory = [](const juce::URL&, const juce::URL::InputStreamOptions& options,
                      const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        if (auto* statusCode = options.getStatusCode())
            *statusCode = 429;
        if (auto* headers = options.getResponseHeaders())
            headers->set("Retry-After", "30");
        return nullptr;
    };

    synth::OllamaProvider provider{"http://mock-host:11434", factory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::RateLimit);
    EXPECT_EQ(result.error.retryAfterSeconds, 30);
}

// REGRESSION LOCK: a genuine connection failure (no HTTP response at all) must map to
// AIErrorKind::Network, distinct from an authenticated-but-rejected request.
TEST_F(OllamaProviderTest, MapsConnectionFailureToNetwork) {
    mockProviderFailingStreams.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    mockProviderFailingStreams.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Network);
}

// REGRESSION LOCK: a 2xx response whose body is not the expected {message:{content:...}}
// shape must map to AIErrorKind::Schema rather than silently reporting a generic failure.
TEST_F(OllamaProviderTest, MapsUnparseableBodyToSchema) {
    auto factory = [](const juce::URL&, const juce::URL::InputStreamOptions&,
                      const synth::OllamaProvider::StreamPublisher&) -> std::unique_ptr<juce::InputStream> {
        return std::make_unique<MockInputStream>("not json at all", false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", factory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

// REGRESSION LOCK: a successful response must carry AIErrorKind::None, so callers can
// treat "no error" as a first-class, checkable value rather than inferring it from success.
TEST_F(OllamaProviderTest, SuccessfulResponseHasNoneErrorKind) {
    mockProviderSuccessfulChat.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    mockProviderSuccessfulChat.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::None);
}

// REGRESSION LOCK: every response — success or failure — must carry the requestId of the
// request it answers, so a caller juggling multiple in-flight requests can match them up.
TEST_F(OllamaProviderTest, EveryResponseCarriesRequestId) {
    mockProviderSuccessfulChat.setModel("mock-model:latest");
    mockProviderFailingStreams.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};

    MockPromptCallback successCallback;
    auto successId = mockProviderSuccessfulChat.sendPrompt(
        conversation, [&successCallback](const synth::AIProvider::AIResponse& response) { successCallback(response); });
    auto successResult = successCallback.getResult();

    MockPromptCallback failureCallback;
    auto failureId = mockProviderFailingStreams.sendPrompt(
        conversation, [&failureCallback](const synth::AIProvider::AIResponse& response) { failureCallback(response); });
    auto failureResult = failureCallback.getResult();

    EXPECT_FALSE(successResult.requestId.isEmpty());
    EXPECT_EQ(successResult.requestId, juce::String(successId.value));

    EXPECT_FALSE(failureResult.requestId.isEmpty());
    EXPECT_EQ(failureResult.requestId, juce::String(failureId.value));
    EXPECT_EQ(failureResult.error.requestId, failureResult.requestId);
}
