// Model/timeout configuration, model discovery, and sendPrompt's request-body shape (selected
// model, sampling options).
#include "OllamaProviderTestFixture.h"

#include "AI/AIStateMapper/AIStateMapper.h"

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

// Unset SamplingOptions must leave the request body without any sampling fields — no
// production caller opts in, so this is the "no behavior change" half of the
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

// Explicit values are opt-in wiring for Tools/AIEvalHarness's reproducibility knobs —
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

// A short chat sends no num_ctx (unchanged body); a prompt past what Ollama's smallest server
// default (4096 tokens) holds gets a power-of-two context big enough for it plus the answer, so
// the server never cuts the front of the production edit-plan prompt.
TEST(OllamaProviderContextLength, SizesContextToThePrompt) {
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(0), 0);
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(30), 0);
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(3000), 0);
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(12000), 8192);
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(20000), 16384);
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(60000), 32768); // about the vendored v12 prompt
    EXPECT_EQ(synth::OllamaProvider::contextLengthFor(100000000), 131072);
}

TEST_F(OllamaProviderTest, SendPromptSendsContextLengthForALongPrompt) {
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

    std::vector<synth::AIProvider::Message> conversation = {{"system", juce::String::repeatedString("a", 60000)},
                                                            {"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });
    callback.getResult();
    provider.stopThread(5000);

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    juce::var options = parsedBody.getProperty("options", juce::var());
    ASSERT_TRUE(options.isObject());
    EXPECT_EQ(static_cast<int>(options.getProperty("num_ctx", juce::var())), 32768);
    EXPECT_FALSE(options.hasProperty("temperature"));
}

// The model stays loaded between edit-plan requests (the server default unloads it after 5 minutes).
TEST_F(OllamaProviderTest, SendPromptAsksOllamaToKeepTheModelLoaded) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options, publish);
        capturedPostData = url.getPostData();
        return std::make_unique<MockInputStream>(
            R"({"model":"mock-model","message":{"role":"assistant","content":"ok"}})", false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"system", "Be brief."}, {"user", "Hello AI"}};
    MockPromptCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });
    callback.getResult();
    provider.stopThread(5000);

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    EXPECT_EQ(parsedBody.getProperty("keep_alive", juce::var()).toString(), juce::String("30m"));
    // The system prompt stays the first message, byte for byte, so Ollama's cached prompt prefix is reused.
    EXPECT_EQ(parsedBody.getProperty("messages", juce::var())[0].getProperty("content", juce::var()).toString(),
              juce::String("Be brief."));
}

TEST(OllamaProviderTimings, SeparatesPromptProcessingFromGeneration) {
    const auto response = juce::JSON::parse(
        R"({"message":{"content":"x"},"prompt_eval_duration":1200000000,"eval_duration":3400000000})");
    EXPECT_EQ(synth::OllamaProvider::describeTimings(response), juce::String("prompt_eval 1.2 s, eval 3.4 s"));
    EXPECT_TRUE(synth::OllamaProvider::describeTimings(juce::JSON::parse(R"({"message":{"content":"x"}})")).isEmpty());
    EXPECT_TRUE(synth::OllamaProvider::describeTimings(juce::var()).isEmpty());
}

TEST(OllamaProviderUsage, ReadsTokenCountsAndDurationsAndLeavesMissingOnesUnknown) {
    const auto usage = synth::OllamaProvider::usageFrom(juce::JSON::parse(
        R"({"prompt_eval_count":5811,"eval_count":1419,"prompt_eval_duration":1200000000,"eval_duration":3400000000})"));
    EXPECT_EQ(usage.promptTokens, 5811);
    EXPECT_EQ(usage.answerTokens, 1419);
    EXPECT_DOUBLE_EQ(usage.promptSeconds, 1.2);
    EXPECT_DOUBLE_EQ(usage.answerSeconds, 3.4);

    const auto none = synth::OllamaProvider::usageFrom(juce::JSON::parse(R"({"message":{"content":"x"}})"));
    EXPECT_EQ(none.promptTokens, -1);
    EXPECT_EQ(none.answerTokens, -1);
    EXPECT_EQ(synth::OllamaProvider::usageFrom(juce::var()).answerTokens, -1);
}

// The eval harness compares answer sizes across runs from these counts, so they must reach the caller.
TEST_F(OllamaProviderTest, ASuccessfulAnswerCarriesTheServersTokenCounts) {
    auto factory = [](const juce::URL&, const juce::URL::InputStreamOptions&,
                      const synth::OllamaProvider::StreamPublisher&) -> std::unique_ptr<juce::InputStream> {
        return std::make_unique<MockInputStream>(
            R"({"message":{"role":"assistant","content":"ok"},"prompt_eval_count":120,"eval_count":45,)"
            R"("prompt_eval_duration":500000000,"eval_duration":2000000000})",
            false);
    };
    synth::OllamaProvider provider{"http://mock-host:11434", factory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    MockPromptCallback callback;
    provider.sendPrompt({{"user", "Hello AI"}},
                        [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });
    const auto response = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(response.success);
    EXPECT_EQ(response.usage.promptTokens, 120);
    EXPECT_EQ(response.usage.answerTokens, 45);
    EXPECT_DOUBLE_EQ(response.usage.answerSeconds, 2.0);
}

// A runaway edit-plan answer must end in seconds, not at the request timeout.
TEST_F(OllamaProviderTest, EditPlanRequestCapsTheAnswerLengthAndOtherRequestsDoNot) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](
            const juce::URL& url, const juce::URL::InputStreamOptions& options,
            const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options, publish);
        capturedPostData = url.getPostData();
        return std::make_unique<MockInputStream>(
            R"({"model":"mock-model","message":{"role":"assistant","content":"ok"}})", false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");
    std::vector<synth::AIProvider::Message> conversation = {{"system", "Be brief."}, {"user", "Hello AI"}};

    auto send = [&](const juce::var& schema) {
        MockPromptCallback callback;
        provider.sendPrompt(
            conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); }, schema);
        callback.getResult();
        return juce::JSON::parse(capturedPostData);
    };

    const juce::var editPlanBody = send(synth::AIStateMapper::getProjectEditSchema());
    EXPECT_EQ(static_cast<int>(editPlanBody.getProperty("options", juce::var()).getProperty("num_predict", 0)), 4096);

    const juce::var patchBody = send(synth::AIStateMapper::getPatchSchema());
    EXPECT_FALSE(patchBody.getProperty("options", juce::var()).hasProperty("num_predict"));

    const juce::var chatBody = send(juce::var());
    EXPECT_FALSE(chatBody.hasProperty("options"));
    provider.stopThread(5000);
}
