// Model/timeout configuration, model discovery, and sendPrompt's request-body shape (selected
// model, sampling options).
#include "OllamaProviderTestFixture.h"

TEST_F(OllamaProviderTest, SetAndGetCurrentModel) {
    juce::String modelName = "test-model:latest";
    mockProviderFailingStreams.setModel(modelName);
    ASSERT_EQ(mockProviderFailingStreams.getCurrentModel(), modelName);
}

TEST_F(OllamaProviderTest, RequestTimeoutMsDefaultsTo240000UnlessChanged) {
    // No regression vs. current (pre-configurability) behavior: a provider that's never had
    // setRequestTimeoutMs() called still uses the same 4-minute ceiling it always has.
    EXPECT_EQ(mockProviderFailingStreams.getRequestTimeoutMs(), 240000);
}

TEST_F(OllamaProviderTest, SetAndGetRequestTimeoutMsRoundTrips) {
    mockProviderFailingStreams.setRequestTimeoutMs(60000);
    EXPECT_EQ(mockProviderFailingStreams.getRequestTimeoutMs(), 60000);
}

TEST_F(OllamaProviderTest, ConfiguredRequestTimeoutMsReachesTheHttpConnectionTimeout) {
    mockProviderCapturingTimeout.setModel("mock-model:latest");
    mockProviderCapturingTimeout.setRequestTimeoutMs(30000);

    std::vector<synth::AIProvider::Message> conversation = {{"user", "hi"}};
    MockPromptCallback callback;
    mockProviderCapturingTimeout.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    mockProviderCapturingTimeout.stopThread(5000);
    ASSERT_TRUE(result.success);
    EXPECT_EQ(lastCapturedTimeoutMs, 30000)
        << "the CONFIGURED timeout must reach withConnectionTimeoutMs(), not just the "
           "kDefaultChatRequestTimeoutMs default";
}

TEST_F(OllamaProviderTest, FetchAvailableModelsFailsGracefullyWithMock) {
    MockCompletionCallback callback;
    mockProviderFailingStreams.fetchAvailableModels(
        [&callback](const juce::StringArray& models, bool success) { callback(models, success); });

    auto result = callback.getResult();
    ASSERT_FALSE(std::get<1>(result));          // Should be unsuccessful
    ASSERT_TRUE(std::get<0>(result).isEmpty()); // Models list should be empty
}

TEST_F(OllamaProviderTest, FetchAvailableModelsSuccessWithMock) {
    MockCompletionCallback callback;
    mockProviderSuccessfulModels.fetchAvailableModels(
        [&callback](const juce::StringArray& models, bool success) { callback(models, success); });

    auto result = callback.getResult();
    ASSERT_TRUE(std::get<1>(result));            // Should be successful
    ASSERT_FALSE(std::get<0>(result).isEmpty()); // Models list should not be empty
    ASSERT_TRUE(std::get<0>(result).contains("mock-model:latest"));
}

TEST_F(OllamaProviderTest, SendPromptSuccessWithMock) {
    mockProviderSuccessfulChat.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    mockProviderSuccessfulChat.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    ASSERT_TRUE(result.success);
    ASSERT_FALSE(result.content.isEmpty());
    ASSERT_TRUE(result.content.contains("Mocked AI response."));
}

// A stream whose read() blocks for ~300ms to simulate a slow/unreachable Ollama
// server during model discovery. Mirrors SlowInputStream but with a fixed delay
// used specifically by the non-blocking discovery test.
class BlockingDiscoveryStream : public juce::InputStream {
public:
    bool failedToOpen() const { return false; }
    juce::int64 getTotalLength() override { return 1; }
    juce::int64 getPosition() override { return readCalled ? 1 : 0; }
    bool setPosition(juce::int64 newPosition) override {
        juce::ignoreUnused(newPosition);
        return false;
    }
    bool isExhausted() override { return readCalled; }

    int read(void* destBuffer, int maxBytesToRead) override {
        juce::ignoreUnused(destBuffer, maxBytesToRead);
        if (readCalled)
            return 0;
        // Sleep in small increments so the worker can still be stopped cleanly.
        int elapsed = 0;
        while (elapsed < 300) {
            if (juce::Thread::currentThreadShouldExit())
                return 0;
            juce::Thread::sleep(50);
            elapsed += 50;
        }
        readCalled = true;
        return 0;
    }

private:
    bool readCalled = false;
};

// REGRESSION LOCK (UI-hang fix): fetchAvailableModels() must NOT block the caller
// (the message thread). Even when the underlying stream is slow/unreachable, the
// call must return immediately because discovery runs on a worker thread. A second
// immediate call must also return immediately (it must not join the first worker).
TEST_F(OllamaProviderTest, FetchAvailableModelsDoesNotBlockCaller) {
    auto blockingFactory =
        [](const juce::URL& url, const juce::URL::InputStreamOptions& options,
           const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(url, options, publish);
        return std::make_unique<BlockingDiscoveryStream>();
    };

    synth::OllamaProvider provider{"http://mock-host:11434", blockingFactory};
    provider.setTestMode(true);

    using clock = std::chrono::steady_clock;

    auto t0 = clock::now();
    provider.fetchAvailableModels([](const juce::StringArray&, bool) {});
    auto firstCallMs = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();

    auto t1 = clock::now();
    provider.fetchAvailableModels([](const juce::StringArray&, bool) {});
    auto secondCallMs = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t1).count();

    EXPECT_LT(firstCallMs, 50) << "First fetchAvailableModels() blocked the caller for " << firstCallMs
                               << " ms (the worker stream sleeps ~300ms; the call must return immediately)";
    EXPECT_LT(secondCallMs, 50) << "Second fetchAvailableModels() blocked the caller for " << secondCallMs
                                << " ms (it must NOT join/wait on the first in-flight discovery)";

    // Let the worker finish so the destructor join is fast and clean.
    provider.stopThread(5000);
}

// REGRESSION LOCK: sendPrompt() must fail fast, without touching the network, when no
// model has been selected. Previously an empty currentModel silently sent
// {"model": ""} to Ollama's /api/chat, which the server rejects with HTTP 400
// "model is required" — surfaced to the user as a misleading "Could not connect"
// error even though the server was perfectly reachable.
TEST_F(OllamaProviderTest, SendPromptWithNoModelFailsWithoutHittingNetwork) {
    bool networkFactoryInvoked = false;
    auto trackingFactory =
        [&networkFactoryInvoked](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(url, options, publish);
        networkFactoryInvoked = true;
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Should not be reached."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", trackingFactory};
    provider.setTestMode(true);
    // Deliberately do NOT call setModel() — currentModel stays empty.

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_FALSE(result.success);
    EXPECT_TRUE(result.error.message.containsIgnoreCase("model"));
    EXPECT_FALSE(networkFactoryInvoked) << "sendPrompt() must not hit the network when no model is selected";
}

// REGRESSION LOCK: direct lock on "we never send an empty model to Ollama". Captures the
// actual POST body sent to /api/chat and verifies the "model" field matches whatever was
// set via setModel().
TEST_F(OllamaProviderTest, SendPromptIncludesSelectedModelInRequestBody) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options, publish);
        capturedPostData = url.getPostData();
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    EXPECT_EQ(parsedBody.getProperty("model", juce::var()).toString(), juce::String("mock-model:latest"));
}

// P6-13: unset SamplingOptions must leave the request body exactly as before this feature
// existed — no production caller opts in, so this is the "no behavior change" half of the
// contract. Mirrors SendPromptIncludesSelectedModelInRequestBody's capture pattern.
TEST_F(OllamaProviderTest, SendPromptOmitsSamplingOptionsWhenUnset) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options, publish);
        capturedPostData = url.getPostData();
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });
    callback.getResult();
    provider.stopThread(5000);

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    EXPECT_FALSE(parsedBody.hasProperty("think"));
    EXPECT_FALSE(parsedBody.hasProperty("options"));
}

// P6-13: explicit values are opt-in wiring for Tools/AIEvalHarness's reproducibility knobs —
// think is top-level (Ollama's reasoning-model switch), temperature/seed nest under "options"
// (Ollama's sampling parameters).
TEST_F(OllamaProviderTest, SendPromptIncludesSamplingOptionsWhenSet) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options, publish);
        capturedPostData = url.getPostData();
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");
    provider.setSamplingOptions({/*think=*/false, /*temperature=*/0.0, /*seed=*/42});

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });
    callback.getResult();
    provider.stopThread(5000);

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    EXPECT_EQ(static_cast<bool>(parsedBody.getProperty("think", juce::var())), false);
    juce::var options = parsedBody.getProperty("options", juce::var());
    ASSERT_TRUE(options.isObject());
    EXPECT_DOUBLE_EQ(static_cast<double>(options.getProperty("temperature", juce::var())), 0.0);
    EXPECT_EQ(static_cast<int>(options.getProperty("seed", juce::var())), 42);
}
