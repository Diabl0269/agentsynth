#include "../Source/AI/OllamaProvider.h" // Correct path
#include <chrono>                        // For steady_clock timing
#include <future>                        // For std::promise/std::future
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>

// Mock AIProvider::CompletionCallback for testing
struct MockCompletionCallback {
    std::promise<std::pair<juce::String, bool>> promise;

    // Operator for fetchAvailableModels (StringArray)
    void operator()(const juce::StringArray& models, bool success) {
        promise.set_value({models.joinIntoString("|"), success});
    }

    // Operator for sendPrompt (String)
    void operator()(const juce::String& response, bool success) { promise.set_value({response, success}); }

    std::pair<juce::String, bool> getResult() { return promise.get_future().get(); }
};

// Mock InputStream for testing network failures/successes
class MockInputStream : public juce::InputStream {
public:
    MockInputStream(const juce::String& content, bool simulateError)
        : buffer(content.toRawUTF8(), content.getNumBytesAsUTF8())
        , currentPosition(0)
        , shouldSimulateError(simulateError) {}

    // Not virtual in juce::InputStream
    bool failedToOpen() const { return shouldSimulateError; }

    juce::int64 getTotalLength() override { return buffer.getSize(); } // Removed const
    juce::int64 getPosition() override { return currentPosition; }     // Removed const
    bool setPosition(juce::int64 newPosition) override {
        if (newPosition >= 0 && newPosition <= getTotalLength()) {
            currentPosition = newPosition;
            return true;
        }
        return false;
    }

    // Must implement this pure virtual method, and it's not const
    bool isExhausted() override { return getPosition() >= getTotalLength(); }

    int read(void* destBuffer, int maxBytesToRead) override {
        if (shouldSimulateError)
            return 0; // Simulate error reading

        auto bytesRemaining = static_cast<int>(getTotalLength() - getPosition());
        auto bytesToRead = juce::jmin(maxBytesToRead, bytesRemaining);

        if (bytesToRead <= 0)
            return 0;

        buffer.copyTo(destBuffer, getPosition(), static_cast<size_t>(bytesToRead));
        currentPosition += bytesToRead;
        return bytesToRead;
    }

private:
    juce::MemoryBlock buffer;
    juce::int64 currentPosition; // Changed to juce::int64
    bool shouldSimulateError;
};

// Mock InputStream that simulates a delay for timeout testing
class SlowInputStream : public juce::InputStream {
public:
    SlowInputStream(int delayMs)
        : delayInMs(delayMs) {}

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
        // Sleep in small increments so the thread can be stopped cleanly
        int elapsed = 0;
        while (elapsed < delayInMs) {
            if (juce::Thread::currentThreadShouldExit())
                return 0;
            juce::Thread::sleep(100);
            elapsed += 100;
        }
        readCalled = true;
        return 0;
    }

private:
    int delayInMs;
    bool readCalled = false;
};

class OllamaProviderTest : public ::testing::Test {
protected:
    // This is set to an invalid host so it tries to connect and fails,
    // allowing us to test error handling for network requests.
    // In a real scenario, you'd mock the network or use a controllable test server.
    // For now, we expect connection failures.
    // synth::OllamaProvider provider{"http://invalid-ollama-host:11434"}; // Replaced by mocked provider
    juce::String validOllamaHost = "http://127.0.0.1:11434"; // Assuming local Ollama instance runs here

    // A factory that always returns a stream simulating an an error (returns nullptr)
    static std::unique_ptr<juce::InputStream> createFailingStream(const juce::URL& url,
                                                                  const juce::URL::InputStreamOptions& options) {
        juce::ignoreUnused(url, options);
        return nullptr; // Simulate failed to open stream
    }

    // A factory that returns a stream with a predefined success response
    static std::unique_ptr<juce::InputStream>
    createSuccessfulModelsStream(const juce::URL& url, const juce::URL::InputStreamOptions& options) {
        juce::ignoreUnused(url, options);
        juce::String jsonResponse = R"({"models":[{"name":"mock-model:latest","model":"mock-model:latest"}]})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    }

    // A factory that returns a stream with a predefined chat response
    static std::unique_ptr<juce::InputStream> createSuccessfulChatStream(const juce::URL& url,
                                                                         const juce::URL::InputStreamOptions& options) {
        juce::ignoreUnused(url, options);
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    }

    // A factory that returns a stream that takes a long time to respond
    static std::unique_ptr<juce::InputStream> createSlowStream(const juce::URL& url,
                                                               const juce::URL::InputStreamOptions& options) {
        juce::ignoreUnused(url, options);
        // Always use a short delay — the test verifies timeout behavior,
        // not the actual timeout duration. 4 seconds is enough.
        return std::make_unique<SlowInputStream>(4000);
    }

    synth::OllamaProvider mockProviderFailingStreams{"http://mock-host:11434", createFailingStream};
    synth::OllamaProvider mockProviderSuccessfulModels{"http://mock-host:11434", createSuccessfulModelsStream};
    synth::OllamaProvider mockProviderSuccessfulChat{"http://mock-host:11434", createSuccessfulChatStream};

    OllamaProviderTest() {
        mockProviderFailingStreams.setTestMode(true);
        mockProviderSuccessfulModels.setTestMode(true);
        mockProviderSuccessfulChat.setTestMode(true);
    }

    void SetUp() override {
        // Ensure providers are stopped before each test
        mockProviderFailingStreams.stopThread(5000);
        mockProviderSuccessfulModels.stopThread(5000);
        mockProviderSuccessfulChat.stopThread(5000);
    }

    void TearDown() override {
        mockProviderFailingStreams.stopThread(5000);
        mockProviderSuccessfulModels.stopThread(5000);
        mockProviderSuccessfulChat.stopThread(5000);
    }
};

TEST_F(OllamaProviderTest, SetAndGetCurrentModel) {
    juce::String modelName = "test-model:latest";
    mockProviderFailingStreams.setModel(modelName);
    ASSERT_EQ(mockProviderFailingStreams.getCurrentModel(), modelName);
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
    MockCompletionCallback callback;
    mockProviderSuccessfulChat.sendPrompt(
        conversation, [&callback](const juce::String& response, bool success) { callback(response, success); });

    auto result = callback.getResult();
    ASSERT_TRUE(std::get<1>(result));            // Should be successful
    ASSERT_FALSE(std::get<0>(result).isEmpty()); // Response should not be empty
    ASSERT_TRUE(std::get<0>(result).contains("Mocked AI response."));
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
    auto blockingFactory = [](const juce::URL& url,
                              const juce::URL::InputStreamOptions& options) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(url, options);
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
        [&networkFactoryInvoked](const juce::URL& url,
                                 const juce::URL::InputStreamOptions& options) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(url, options);
        networkFactoryInvoked = true;
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Should not be reached."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", trackingFactory};
    provider.setTestMode(true);
    // Deliberately do NOT call setModel() — currentModel stays empty.

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockCompletionCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const juce::String& response, bool success) { callback(response, success); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_FALSE(std::get<1>(result)); // success == false
    EXPECT_TRUE(std::get<0>(result).containsIgnoreCase("model"));
    EXPECT_FALSE(networkFactoryInvoked) << "sendPrompt() must not hit the network when no model is selected";
}

// REGRESSION LOCK: direct lock on "we never send an empty model to Ollama". Captures the
// actual POST body sent to /api/chat and verifies the "model" field matches whatever was
// set via setModel().
TEST_F(OllamaProviderTest, SendPromptIncludesSelectedModelInRequestBody) {
    juce::String capturedPostData;
    auto capturingFactory =
        [&capturedPostData](const juce::URL& url,
                            const juce::URL::InputStreamOptions& options) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(options);
        capturedPostData = url.getPostData();
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    };

    synth::OllamaProvider provider{"http://mock-host:11434", capturingFactory};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};
    MockCompletionCallback callback;
    provider.sendPrompt(conversation,
                        [&callback](const juce::String& response, bool success) { callback(response, success); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(std::get<1>(result)); // success == true

    juce::var parsedBody = juce::JSON::parse(capturedPostData);
    ASSERT_TRUE(parsedBody.isObject());
    EXPECT_EQ(parsedBody.getProperty("model", juce::var()).toString(), juce::String("mock-model:latest"));
}

TEST_F(OllamaProviderTest, SendPromptTimeoutFails) {
    // Create a provider that uses the slow stream factory
    synth::OllamaProvider mockProviderSlowStream{"http://mock-host:11434", createSlowStream};
    mockProviderSlowStream.setTestMode(true);
    mockProviderSlowStream.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Simulate timeout"}};
    MockCompletionCallback callback;
    mockProviderSlowStream.sendPrompt(
        conversation, [&callback](const juce::String& response, bool success) { callback(response, success); });

    auto result = callback.getResult();
    mockProviderSlowStream.stopThread(5000);
    ASSERT_FALSE(std::get<1>(result)); // Should be unsuccessful
    // The response text on timeout is "Error: Could not connect to Ollama at "
    // So we can check for that string or a part of it.
    ASSERT_TRUE(std::get<0>(result).isEmpty()); // The response text should be empty string on timeout
}
