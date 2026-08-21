#include "../Source/AI/OllamaProvider.h" // Correct path
#include <atomic>
#include <chrono>             // For steady_clock timing
#include <condition_variable> // For bounded callback waits
#include <future>             // For std::promise/std::future
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>
#include <thread> // For racing cancel() against completion

// Mock AIProvider::CompletionCallback for testing
struct MockCompletionCallback {
    std::promise<std::pair<juce::String, bool>> promise;

    // Operator for fetchAvailableModels (StringArray)
    void operator()(const juce::StringArray& models, bool success) {
        promise.set_value({models.joinIntoString("|"), success});
    }

    std::pair<juce::String, bool> getResult() { return promise.get_future().get(); }
};

struct MockPromptCallback {
    std::promise<synth::AIProvider::AIResponse> promise;

    void operator()(const synth::AIProvider::AIResponse& response) { promise.set_value(response); }

    synth::AIProvider::AIResponse getResult() { return promise.get_future().get(); }
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

namespace {

// Bounded-wait latch for "did the callback fire?" assertions. Every wait has a
// timeout so a lost request fails the test instead of hanging it, and no test ever
// sleeps for a fixed period and hopes the work happened.
class CallbackLatch {
public:
    void fire() {
        {
            const std::lock_guard<std::mutex> lock(mutex);
            fired = true;
        }
        cv.notify_all();
    }

    bool waitFor(std::chrono::milliseconds timeout) {
        std::unique_lock<std::mutex> lock(mutex);
        return cv.wait_for(lock, timeout, [this] { return fired; });
    }

    bool hasFired() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return fired;
    }

private:
    mutable std::mutex mutex;
    std::condition_variable cv;
    bool fired = false;
};

constexpr std::chrono::milliseconds kCallbackTimeout{10000};

// A stream that parks the worker inside read() until its thread is asked to exit, so a
// test can pin the worker on one request while queueing more behind it.
class GatedInputStream : public juce::InputStream {
public:
    explicit GatedInputStream(std::shared_ptr<CallbackLatch> enteredLatch)
        : entered(std::move(enteredLatch)) {}

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

        if (entered != nullptr)
            entered->fire();

        // Hard cap so a broken shutdown path fails the test rather than hanging it.
        int elapsedMs = 0;
        while (!juce::Thread::currentThreadShouldExit() && elapsedMs < 15000) {
            juce::Thread::sleep(5);
            elapsedMs += 5;
        }

        readCalled = true;
        return 0;
    }

private:
    std::shared_ptr<CallbackLatch> entered;
    bool readCalled = false;
};

// The thing a gated read parks on. Deliberately owned separately from the stream (both hold a
// shared_ptr): the worker destroys its stream the moment a cancelled read returns, so a test
// thread that released the gate through the stream itself would be racing a use-after-free.
// Releasing through the gate is safe whatever the stream's lifetime is doing.
struct StreamGate {
    // Manual reset, so a release that lands before read() is even entered still counts.
    juce::WaitableEvent released{true};

    void release() { released.signal(); }

    // Hard cap so a cancel that never arrives fails the test instead of hanging it. A working
    // cancel returns from here in microseconds, so the cap is never hit on the happy path.
    void awaitRelease() { released.wait(15000); }
};

// A stream that parks inside read() until the provider cancels it (via CancellableStream — the
// same hook a real juce::WebInputStream services through its own cancel()) or the test releases
// the gate directly. This is how a test pins a request "mid-flight" and then proves cancel() is
// what freed it.
class CancellableGatedStream
    : public juce::InputStream
    , public synth::CancellableStream {
public:
    CancellableGatedStream(std::shared_ptr<CallbackLatch> enteredLatch, std::shared_ptr<StreamGate> streamGate)
        : entered(std::move(enteredLatch))
        , gate(std::move(streamGate)) {}

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

        readCalled = true;

        if (entered != nullptr)
            entered->fire();

        gate->awaitRelease();
        return 0;
    }

    void cancelRead() override { gate->release(); }

private:
    std::shared_ptr<CallbackLatch> entered;
    std::shared_ptr<StreamGate> gate;
    bool readCalled = false;
};

// Gates the FIRST request and hands later requests an ordinary successful response, so a test can
// cancel one in-flight request and still watch the next one complete.
struct CancellableGatedFactory {
    std::shared_ptr<CallbackLatch> entered = std::make_shared<CallbackLatch>();
    std::shared_ptr<StreamGate> gate = std::make_shared<StreamGate>();
    std::shared_ptr<std::atomic<int>> callCount = std::make_shared<std::atomic<int>>(0);

    synth::OllamaProvider::InputStreamFactory operator()() const {
        auto enteredLatch = entered;
        auto sharedGate = gate;
        auto counter = callCount;

        return [enteredLatch, sharedGate,
                counter](const juce::URL& url, const juce::URL::InputStreamOptions& options,
                         const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
            juce::ignoreUnused(url, options);

            if (counter->fetch_add(1) == 0) {
                auto stream = std::make_unique<CancellableGatedStream>(enteredLatch, sharedGate);
                // Published before returning, exactly as the production factory does before it
                // connects — without this the provider has no handle to abort and cancel() could
                // not reach the gated read at all.
                publish(stream.get());
                return stream;
            }

            return std::make_unique<MockInputStream>(
                R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})", false);
        };
    }
};

// Builds a factory that gates only the FIRST request - later requests get an ordinary
// successful chat response, so a test can pin one worker without stalling the next.
inline synth::OllamaProvider::InputStreamFactory makeOneShotGatedFactory(std::shared_ptr<CallbackLatch> enteredLatch) {
    auto callCount = std::make_shared<std::atomic<int>>(0);

    return [enteredLatch,
            callCount](const juce::URL& url, const juce::URL::InputStreamOptions& options,
                       const synth::OllamaProvider::StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        juce::ignoreUnused(url, options, publish);

        if (callCount->fetch_add(1) == 0)
            return std::make_unique<GatedInputStream>(enteredLatch);

        return std::make_unique<MockInputStream>(
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})", false);
    };
}

} // namespace

class OllamaProviderTest : public ::testing::Test {
protected:
    // This is set to an invalid host so it tries to connect and fails,
    // allowing us to test error handling for network requests.
    // In a real scenario, you'd mock the network or use a controllable test server.
    // For now, we expect connection failures.
    // synth::OllamaProvider provider{"http://invalid-ollama-host:11434"}; // Replaced by mocked provider
    juce::String validOllamaHost = "http://127.0.0.1:11434"; // Assuming local Ollama instance runs here

    // A factory that always returns a stream simulating an an error (returns nullptr)
    static std::unique_ptr<juce::InputStream>
    createFailingStream(const juce::URL& url, const juce::URL::InputStreamOptions& options,
                        const synth::OllamaProvider::StreamPublisher& publish) {
        juce::ignoreUnused(url, options, publish);
        return nullptr; // Simulate failed to open stream
    }

    // A factory that returns a stream with a predefined success response
    static std::unique_ptr<juce::InputStream>
    createSuccessfulModelsStream(const juce::URL& url, const juce::URL::InputStreamOptions& options,
                                 const synth::OllamaProvider::StreamPublisher& publish) {
        juce::ignoreUnused(url, options, publish);
        juce::String jsonResponse = R"({"models":[{"name":"mock-model:latest","model":"mock-model:latest"}]})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    }

    // A factory that returns a stream with a predefined chat response
    static std::unique_ptr<juce::InputStream>
    createSuccessfulChatStream(const juce::URL& url, const juce::URL::InputStreamOptions& options,
                               const synth::OllamaProvider::StreamPublisher& publish) {
        juce::ignoreUnused(url, options, publish);
        juce::String jsonResponse =
            R"({"model":"mock-model","message":{"role":"assistant","content":"Mocked AI response."}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    }

    // A factory that returns a stream that takes a long time to respond
    static std::unique_ptr<juce::InputStream> createSlowStream(const juce::URL& url,
                                                               const juce::URL::InputStreamOptions& options,
                                                               const synth::OllamaProvider::StreamPublisher& publish) {
        juce::ignoreUnused(url, options, publish);
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

// REGRESSION LOCK (request-loss race): a request that is enqueued while the worker
// thread is winding down must still get a callback. The old run() left its loop the
// moment the queue drained or an exit was signalled, while sendPrompt() only started a
// thread `if (!isThreadRunning())` — and juce::Thread reports "running" until after
// run() has returned. A request landing in that window sat in the queue with nothing to
// pick it up, so the UI waited forever with no error. Success or failure is an
// acceptable outcome here; silence is not.
TEST_F(OllamaProviderTest, QueuedRequestDuringThreadShutdownStillCompletes) {
    const std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};

    // Part 1 - deterministic. Pin the worker inside the first request's stream read, put
    // a second request in the queue behind it, then ask the worker to stop. The worker is
    // now unambiguously "winding down" with a queued request in hand, and no timing luck
    // is involved: the request is enqueued before stopThread() is even called.
    {
        auto workerEntered = std::make_shared<CallbackLatch>();
        auto queuedBehind = std::make_shared<CallbackLatch>();

        synth::OllamaProvider provider{"http://mock-host:11434", makeOneShotGatedFactory(workerEntered)};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        provider.sendPrompt(conversation, [](const synth::AIProvider::AIResponse&) {});
        ASSERT_TRUE(workerEntered->waitFor(kCallbackTimeout)) << "the worker never started the first request";

        provider.sendPrompt(conversation,
                            [queuedBehind](const synth::AIProvider::AIResponse&) { queuedBehind->fire(); });

        provider.stopThread(5000);

        EXPECT_TRUE(queuedBehind->waitFor(kCallbackTimeout))
            << "a request queued behind an in-flight one was dropped when the worker wound down";
    }

    // Part 2 - sweeps the microsecond-wide handover window. Each attempt drains the
    // worker and re-enqueues at a slightly different offset into its exit path, so some
    // attempts land in the gap where run() has returned but juce still reports the thread
    // as running. The offset only steers *where* the request lands; the assertion is the
    // same invariant either way, so an attempt that misses the window still has to pass.
    {
        synth::OllamaProvider provider{"http://mock-host:11434", createSuccessfulChatStream};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        constexpr int kAttempts = 40;

        for (int attempt = 0; attempt < kAttempts; ++attempt) {
            // The drain callback is delivered on the worker thread from inside
            // processRequest(), i.e. just before the worker looks at the queue again.
            auto drained = std::make_shared<CallbackLatch>();
            provider.sendPrompt(conversation, [drained](const synth::AIProvider::AIResponse&) { drained->fire(); });
            ASSERT_TRUE(drained->waitFor(kCallbackTimeout))
                << "attempt " << attempt << ": first request never completed";

            // Bounded busy-wait (<= 200 us, never a sleep) to shift the enqueue across
            // the worker's exit path.
            const auto spinUntil = std::chrono::steady_clock::now() + std::chrono::microseconds(attempt * 5);
            while (std::chrono::steady_clock::now() < spinUntil) {
            }

            auto raced = std::make_shared<CallbackLatch>();
            provider.sendPrompt(conversation, [raced](const synth::AIProvider::AIResponse&) { raced->fire(); });
            ASSERT_TRUE(raced->waitFor(kCallbackTimeout))
                << "attempt " << attempt
                << ": a request enqueued while the worker was winding down never got a callback";

            // Also cover a fully completed stop: the handle may still be set here, which
            // is exactly where startThread() used to silently no-op.
            provider.stopThread(5000);

            auto afterShutdown = std::make_shared<CallbackLatch>();
            provider.sendPrompt(conversation,
                                [afterShutdown](const synth::AIProvider::AIResponse&) { afterShutdown->fire(); });
            ASSERT_TRUE(afterShutdown->waitFor(kCallbackTimeout))
                << "attempt " << attempt << ": a request enqueued right after stopThread() never got a callback";
        }

        provider.stopThread(5000);
    }
}

// NOTE: sendPrompt()'s "owner vanished" recovery (a worker that ended without handing
// the queue back) has no test here on purpose. The only ways to reach that state are a
// force-kill via stopThread(0) or a throwing run(), and forcing the first aborts the
// process under glibc: pthread_cancel's forced unwind hits the `catch (...)` in juce's
// threadEntryPoint, which never rethrows ("FATAL: exception not rethrown"). See the
// comment on that branch in OllamaProvider::sendPrompt().

// REGRESSION LOCK: requests still sitting in the queue when the provider is destroyed
// must be failed with a callback, not dropped — and that callback must fire *before*
// destruction completes, so nothing can call back into a dead object.
TEST_F(OllamaProviderTest, PendingRequestsAreFailedOnDestruction) {
    auto workerEntered = std::make_shared<CallbackLatch>();
    auto inFlight = std::make_shared<CallbackLatch>();
    auto queuedFirst = std::make_shared<CallbackLatch>();
    auto queuedSecond = std::make_shared<CallbackLatch>();

    const std::vector<synth::AIProvider::Message> conversation = {{"user", "Hello AI"}};

    {
        synth::OllamaProvider provider{"http://mock-host:11434", makeOneShotGatedFactory(workerEntered)};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        // Pin the worker inside the first request's stream read.
        provider.sendPrompt(conversation, [inFlight](const synth::AIProvider::AIResponse&) { inFlight->fire(); });
        ASSERT_TRUE(workerEntered->waitFor(kCallbackTimeout)) << "the worker never started the first request";

        // These two are still queued, unstarted, when the provider goes away.
        provider.sendPrompt(conversation, [queuedFirst](const synth::AIProvider::AIResponse&) { queuedFirst->fire(); });
        provider.sendPrompt(conversation,
                            [queuedSecond](const synth::AIProvider::AIResponse&) { queuedSecond->fire(); });
    } // ~OllamaProvider(): stops the worker and fails whatever is left in the queue

    // Checked without waiting: by the time the destructor has returned, every callback
    // must already have run.
    EXPECT_TRUE(inFlight->hasFired()) << "the in-flight request got no callback at shutdown";
    EXPECT_TRUE(queuedFirst->hasFired()) << "a request still queued at destruction was dropped without a callback";
    EXPECT_TRUE(queuedSecond->hasFired()) << "a request still queued at destruction was dropped without a callback";
}

TEST_F(OllamaProviderTest, SendPromptTimeoutFails) {
    // Create a provider that uses the slow stream factory
    synth::OllamaProvider mockProviderSlowStream{"http://mock-host:11434", createSlowStream};
    mockProviderSlowStream.setTestMode(true);
    mockProviderSlowStream.setModel("mock-model:latest");

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Simulate timeout"}};
    MockPromptCallback callback;
    mockProviderSlowStream.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& response) { callback(response); });

    auto result = callback.getResult();
    mockProviderSlowStream.stopThread(5000);
    ASSERT_FALSE(result.success);
    ASSERT_TRUE(result.content.isEmpty());
}

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

// ============================================================================
// Cancellation
//
// The Cancel button used to be cosmetic: it hid the spinner while the HTTP request ran to
// completion, so a metered backend still billed for the abandoned work and the serial request
// queue still made the user's next message wait behind it. These lock the real behaviour in.
// ============================================================================

// A cancelled request must still get its callback — exactly one — and it must say Cancelled, not
// "connection failed". An aborted read is indistinguishable from a dead socket at the stream
// level, so reporting it as a network error would send the user off debugging a problem they
// created on purpose.
TEST_F(OllamaProviderTest, CancelledRequestInvokesCallbackWithCancelledKind) {
    CancellableGatedFactory factory;

    // Declared before the provider so it outlives it: the provider's destructor is a delivery path
    // too, and a callback observing destroyed locals would be a crash, not a failure.
    auto done = std::make_shared<CallbackLatch>();
    std::atomic<int> callCount{0};
    std::atomic<bool> reportedSuccess{true};
    std::atomic<synth::AIProvider::AIErrorKind> reportedKind{synth::AIProvider::AIErrorKind::None};

    synth::OllamaProvider provider{"http://mock-host:11434", factory()};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    const auto id = provider.sendPrompt({{"user", "Write me a very long story"}},
                                        [&](const synth::AIProvider::AIResponse& response) {
                                            callCount.fetch_add(1);
                                            reportedSuccess.store(response.success);
                                            reportedKind.store(response.error.kind);
                                            done->fire();
                                        });

    ASSERT_NE(id.value, 0u) << "sendPrompt() must hand back a cancellable handle";
    ASSERT_TRUE(factory.entered->waitFor(kCallbackTimeout)) << "the worker never reached the gated read";

    provider.cancel(id);

    ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "cancel() left the caller hanging with no callback";
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_FALSE(reportedSuccess.load());
    EXPECT_EQ(reportedKind.load(), synth::AIProvider::AIErrorKind::Cancelled);

    provider.stopThread(5000);
}

// The queue is drained serially, so an abandoned request that stays in it (or keeps the worker
// parked in a read) delays every message sent afterwards. Cancelling must free the worker, not
// just stop reporting to the UI.
TEST_F(OllamaProviderTest, CancelDoesNotBlockSubsequentRequest) {
    CancellableGatedFactory factory;

    // Declared before the provider so the latches outlive every delivery path, destructor included.
    auto firstDone = std::make_shared<CallbackLatch>();
    auto secondDone = std::make_shared<CallbackLatch>();
    std::atomic<bool> secondSucceeded{false};

    synth::OllamaProvider provider{"http://mock-host:11434", factory()};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    const auto first = provider.sendPrompt({{"user", "Long one"}},
                                           [firstDone](const synth::AIProvider::AIResponse&) { firstDone->fire(); });

    ASSERT_TRUE(factory.entered->waitFor(kCallbackTimeout)) << "the worker never reached the gated read";

    provider.cancel(first);

    // Sent immediately after the cancel, exactly as a user retyping would. It must be answered on
    // its own merits rather than waiting out the abandoned generation — whose gate would otherwise
    // hold the worker for 15 s.
    provider.sendPrompt({{"user", "Short one"}},
                        [secondDone, &secondSucceeded](const synth::AIProvider::AIResponse& r) {
                            secondSucceeded.store(r.success);
                            secondDone->fire();
                        });

    using clock = std::chrono::steady_clock;
    const auto t0 = clock::now();
    ASSERT_TRUE(secondDone->waitFor(kCallbackTimeout)) << "the request sent after a cancel never completed";
    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(clock::now() - t0).count();

    EXPECT_TRUE(secondSucceeded.load());
    EXPECT_LT(elapsedMs, 5000) << "the second request waited " << elapsedMs
                               << " ms — it is queued behind the cancelled request instead of replacing it";

    EXPECT_TRUE(firstDone->waitFor(kCallbackTimeout)) << "the cancelled request never got its callback";

    provider.stopThread(5000);
}

// Cancelling a request that was never issued, already finished, or already cancelled must do
// nothing at all. The UI holds stale handles routinely (a response and a Cancel click cross), so
// this is the common case, not a defensive edge case.
TEST_F(OllamaProviderTest, CancelOfUnknownRequestIdIsSafeNoOp) {
    synth::OllamaProvider provider{"http://mock-host:11434", createSuccessfulChatStream};
    provider.setTestMode(true);
    provider.setModel("mock-model:latest");

    // Never issued, and the reserved "nothing in flight" handle.
    provider.cancel(synth::AIProvider::RequestId{});
    provider.cancel(synth::AIProvider::RequestId{999999});

    // Already completed: cancelling must not resurrect it or fire a second callback.
    std::atomic<int> callCount{0};
    auto done = std::make_shared<CallbackLatch>();
    const auto id = provider.sendPrompt({{"user", "Hello"}}, [&](const synth::AIProvider::AIResponse&) {
        callCount.fetch_add(1);
        done->fire();
    });
    ASSERT_TRUE(done->waitFor(kCallbackTimeout));

    provider.cancel(id);
    provider.cancel(id); // ...and again, to cover double-cancel

    // A stray callback would arrive on the worker thread; give it room to show up.
    juce::Thread::sleep(200);
    EXPECT_EQ(callCount.load(), 1) << "cancelling a completed request fired an extra callback";

    provider.stopThread(5000);
}

// THE race: cancel() and the worker's completion path can reach the same request at the same
// instant. Both want to finish it, and a caller that gets two callbacks corrupts chat history (or,
// in the UI, tears down the waiting state twice). Run with --gtest_repeat=100.
//
// Each iteration sweeps the cancel across the completion by a growing offset. That matters:
// spawning both threads with no offset is NOT enough, because cancel() sets the cancelled flag
// before it releases the read, so the worker always observes the cancel and "completion wins"
// never actually gets exercised — measured, it was 0 out of 1000 iterations. Offset 0 keeps the
// simultaneous case; the later offsets give the completion a real head start.
TEST_F(OllamaProviderTest, CallbackFiresExactlyOnceEvenIfCancelRacesCompletion) {
    constexpr int kIterations = 25;

    for (int i = 0; i < kIterations; ++i) {
        CancellableGatedFactory factory;

        // Both declared before the provider so they outlive it — the destructor is itself a
        // delivery path, and a late callback reading destroyed locals would crash the suite
        // instead of failing this assertion.
        std::atomic<int> callCount{0};
        auto done = std::make_shared<CallbackLatch>();

        synth::OllamaProvider provider{"http://mock-host:11434", factory()};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        const auto id = provider.sendPrompt({{"user", "Race me"}}, [&](const synth::AIProvider::AIResponse&) {
            callCount.fetch_add(1);
            done->fire();
        });

        ASSERT_TRUE(factory.entered->waitFor(kCallbackTimeout)) << "iteration " << i << ": worker never started";

        // The completion is released through the shared gate, not the stream — the worker destroys
        // its stream as soon as a cancelled read returns, so poking the stream from here would be
        // racing a use-after-free rather than testing anything.
        auto gate = factory.gate;
        const auto cancelOffsetUs = std::chrono::microseconds(i * 4);

        std::thread completer([gate]() { gate->release(); });
        std::thread canceller([&provider, id, cancelOffsetUs]() {
            // Bounded busy-wait, never a sleep, so the offset stays in the same order of magnitude
            // as the delivery path it is sweeping across.
            const auto spinUntil = std::chrono::steady_clock::now() + cancelOffsetUs;
            while (std::chrono::steady_clock::now() < spinUntil) {
            }
            provider.cancel(id);
        });

        completer.join();
        canceller.join();

        ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "iteration " << i << ": no callback fired at all";

        provider.stopThread(5000);

        // Checked after the worker is fully stopped, so a second delivery has had every
        // opportunity to appear.
        EXPECT_EQ(callCount.load(), 1) << "iteration " << i << ": callback fired " << callCount.load()
                                       << " times — cancel and completion both delivered";
    }
}

// The two orderings above, pinned down deterministically rather than left to timing. Both must
// produce exactly one callback, and the kind must reflect who actually won.
TEST_F(OllamaProviderTest, CancelAndCompletionEachDeliverExactlyOnceInEitherOrder) {
    // Ordering 1: the response lands first, then a late cancel arrives on a stale handle.
    {
        CancellableGatedFactory factory;

        std::atomic<int> callCount{0};
        std::atomic<synth::AIProvider::AIErrorKind> kind{synth::AIProvider::AIErrorKind::Cancelled};
        auto done = std::make_shared<CallbackLatch>();

        synth::OllamaProvider provider{"http://mock-host:11434", factory()};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        const auto id =
            provider.sendPrompt({{"user", "Finish first"}}, [&](const synth::AIProvider::AIResponse& response) {
                callCount.fetch_add(1);
                kind.store(response.error.kind);
                done->fire();
            });

        ASSERT_TRUE(factory.entered->waitFor(kCallbackTimeout));

        factory.gate->release();
        ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "the request never completed";

        provider.cancel(id); // stale handle — must be inert

        provider.stopThread(5000);
        EXPECT_EQ(callCount.load(), 1) << "a cancel after completion produced a second callback";
        EXPECT_NE(kind.load(), synth::AIProvider::AIErrorKind::Cancelled)
            << "a request that completed normally was reported as cancelled";
    }

    // Ordering 2: the cancel lands first, then the response the worker was waiting on arrives
    // anyway (the server had already started sending). The late data must be discarded.
    {
        CancellableGatedFactory factory;

        std::atomic<int> callCount{0};
        std::atomic<synth::AIProvider::AIErrorKind> kind{synth::AIProvider::AIErrorKind::None};
        auto done = std::make_shared<CallbackLatch>();

        synth::OllamaProvider provider{"http://mock-host:11434", factory()};
        provider.setTestMode(true);
        provider.setModel("mock-model:latest");

        const auto id =
            provider.sendPrompt({{"user", "Cancel first"}}, [&](const synth::AIProvider::AIResponse& response) {
                callCount.fetch_add(1);
                kind.store(response.error.kind);
                done->fire();
            });

        ASSERT_TRUE(factory.entered->waitFor(kCallbackTimeout));

        provider.cancel(id);
        ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "cancel left the caller hanging";

        factory.gate->release(); // the response arriving too late

        provider.stopThread(5000);
        EXPECT_EQ(callCount.load(), 1) << "a late response produced a second callback after cancellation";
        EXPECT_EQ(kind.load(), synth::AIProvider::AIErrorKind::Cancelled);
    }
}
