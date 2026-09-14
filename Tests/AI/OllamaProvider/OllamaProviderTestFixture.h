// Shared fixture: mock streams/callbacks, the gated-cancellation helpers, and the
// OllamaProviderTest test-class used across every OllamaProvider*Tests.cpp topic file.
#pragma once

#include "AI/OllamaProvider.h" // Correct path
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

    // Captures the connection timeout each sendPrompt() call was actually given, so a test can
    // confirm the instance's configured requestTimeoutMs (not just kDefaultChatRequestTimeoutMs)
    // reaches the HTTP layer, not just that the getter/setter round-trips.
    static int lastCapturedTimeoutMs;
    static std::unique_ptr<juce::InputStream>
    createTimeoutCapturingStream(const juce::URL& url, const juce::URL::InputStreamOptions& options,
                                 const synth::OllamaProvider::StreamPublisher& publish) {
        juce::ignoreUnused(url, publish);
        lastCapturedTimeoutMs = options.getConnectionTimeoutMs();
        juce::String jsonResponse = R"({"model":"mock-model","message":{"role":"assistant","content":"ok"}})";
        return std::make_unique<MockInputStream>(jsonResponse, false);
    }

    synth::OllamaProvider mockProviderFailingStreams{"http://mock-host:11434", createFailingStream};
    synth::OllamaProvider mockProviderSuccessfulModels{"http://mock-host:11434", createSuccessfulModelsStream};
    synth::OllamaProvider mockProviderSuccessfulChat{"http://mock-host:11434", createSuccessfulChatStream};
    synth::OllamaProvider mockProviderCapturingTimeout{"http://mock-host:11434", createTimeoutCapturingStream};

    OllamaProviderTest() {
        mockProviderFailingStreams.setTestMode(true);
        mockProviderSuccessfulModels.setTestMode(true);
        mockProviderSuccessfulChat.setTestMode(true);
        mockProviderCapturingTimeout.setTestMode(true);
    }

    void SetUp() override {
        // Ensure providers are stopped before each test
        mockProviderFailingStreams.stopThread(5000);
        mockProviderSuccessfulModels.stopThread(5000);
        mockProviderSuccessfulChat.stopThread(5000);
        mockProviderCapturingTimeout.stopThread(5000);
    }

    void TearDown() override {
        mockProviderFailingStreams.stopThread(5000);
        mockProviderSuccessfulModels.stopThread(5000);
        mockProviderSuccessfulChat.stopThread(5000);
        mockProviderCapturingTimeout.stopThread(5000);
    }
};

// inline: this header is included by every OllamaProvider*Tests.cpp topic file, so the static
// member's definition must not be duplicated per translation unit (C++17 inline variable).
inline int OllamaProviderTest::lastCapturedTimeoutMs = -1;
