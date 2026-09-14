// Transport-level failures (no HTTP response at all), cancellation of queued/in-flight requests,
// and misc AIProvider interface conformance.
#include "RemoteProviderTestFixture.h"

TEST_F(RemoteProviderTest, TransportFailureMapsToNetwork) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        synth::RemoteProvider::HttpResult result;
        result.transportFailed = true;
        result.errorMessage = "Could not resolve host";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Network);
}

TEST_F(RemoteProviderTest, TimeoutMapsToTimeout) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        synth::RemoteProvider::HttpResult result;
        result.timedOut = true;
        result.errorMessage = "timed out";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Timeout);
}

// A request still sitting in pendingRequests (never dequeued) when cancel() is called must be
// delivered Cancelled promptly, and the fake performer must never be invoked for it at all.
TEST_F(RemoteProviderTest, CancellingQueuedRequestDeliversCancelledWithoutInvokingPerformer) {
    auto firstEntered = std::make_shared<CallbackLatch>();
    auto firstGate = std::make_shared<CallbackLatch>(); // released to let the first request finish
    std::atomic<int> performerCallCount{0};

    auto performer = [firstEntered, firstGate,
                      &performerCallCount](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                                           const std::atomic<bool>& cancelled) -> synth::RemoteProvider::HttpResult {
        const int callIndex = performerCallCount.fetch_add(1);
        if (callIndex == 0) {
            firstEntered->fire();
            // Poll like a real curl progress callback would, but this request is never cancelled
            // in this test — it is released explicitly once the assertions below are done.
            int elapsedMs = 0;
            while (!firstGate->hasFired() && !cancelled.load() && elapsedMs < 15000) {
                juce::Thread::sleep(5);
                elapsedMs += 5;
            }
            return makeSuccess(R"({"data":{}})");
        }
        // Must never be reached: request 2 is cancelled while still queued, before the worker
        // ever pops it.
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    auto firstDone = std::make_shared<CallbackLatch>();
    provider.sendPrompt(
        {{"user", "first (long)"}}, [firstDone](const synth::AIProvider::AIResponse&) { firstDone->fire(); },
        makeSchema());
    ASSERT_TRUE(firstEntered->waitFor(kCallbackTimeout)) << "the worker never started the first request";

    std::atomic<int> secondCallCount{0};
    std::atomic<synth::AIProvider::AIErrorKind> secondKind{synth::AIProvider::AIErrorKind::None};
    auto secondDone = std::make_shared<CallbackLatch>();
    const auto secondId = provider.sendPrompt(
        {{"user", "second (should be cancelled while queued)"}},
        [&secondCallCount, &secondKind, secondDone](const synth::AIProvider::AIResponse& r) {
            secondCallCount.fetch_add(1);
            secondKind.store(r.error.kind);
            secondDone->fire();
        },
        makeSchema());

    provider.cancel(secondId);

    ASSERT_TRUE(secondDone->waitFor(kCallbackTimeout)) << "cancelling a queued request left the caller hanging";
    EXPECT_EQ(secondCallCount.load(), 1);
    EXPECT_EQ(secondKind.load(), synth::AIProvider::AIErrorKind::Cancelled);
    EXPECT_EQ(performerCallCount.load(), 1) << "the fake performer must not be invoked for a request cancelled "
                                               "while it was still queued";

    firstGate->fire();
    ASSERT_TRUE(firstDone->waitFor(kCallbackTimeout)) << "the first request never completed";

    provider.stopThread(5000);
}

// A request whose fake performer is currently "in flight" must notice cancellation via the
// `cancelled` atomic it was handed — exactly what CURLOPT_XFERINFOFUNCTION does for real curl —
// and the provider must deliver Cancelled once the performer returns.
TEST_F(RemoteProviderTest, CancellingInFlightRequestDeliversCancelledOnceNoticed) {
    auto entered = std::make_shared<CallbackLatch>();

    auto performer = [entered](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                               const std::atomic<bool>& cancelled) -> synth::RemoteProvider::HttpResult {
        entered->fire();
        int elapsedMs = 0;
        while (!cancelled.load() && elapsedMs < 15000) {
            juce::Thread::sleep(5);
            elapsedMs += 5;
        }
        synth::RemoteProvider::HttpResult result;
        result.transportFailed = true; // simulates CURLE_ABORTED_BY_CALLBACK; irrelevant to the
                                       // caller, which checks `cancelled` before looking at this.
        result.errorMessage = "aborted";
        return result;
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::atomic<int> callCount{0};
    std::atomic<synth::AIProvider::AIErrorKind> kind{synth::AIProvider::AIErrorKind::None};
    auto done = std::make_shared<CallbackLatch>();

    const auto id = provider.sendPrompt(
        {{"user", "long generation"}},
        [&callCount, &kind, done](const synth::AIProvider::AIResponse& r) {
            callCount.fetch_add(1);
            kind.store(r.error.kind);
            done->fire();
        },
        makeSchema());

    ASSERT_TRUE(entered->waitFor(kCallbackTimeout)) << "the worker never reached the fake in-flight performer";

    provider.cancel(id);

    ASSERT_TRUE(done->waitFor(kCallbackTimeout)) << "cancel() left the caller hanging with no callback";
    EXPECT_EQ(callCount.load(), 1);
    EXPECT_EQ(kind.load(), synth::AIProvider::AIErrorKind::Cancelled);

    provider.stopThread(5000);
}

TEST_F(RemoteProviderTest, ProviderNameIsRemote) {
    synth::RemoteProvider provider{kMockHost};
    EXPECT_EQ(provider.getProviderName(), juce::String("Remote"));
}

TEST_F(RemoteProviderTest, FetchAvailableModelsReturnsEmptyListWithSuccess) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        ADD_FAILURE() << "fetchAvailableModels must not hit the network";
        return {};
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockCompletionCallback callback;
    provider.fetchAvailableModels(
        [&callback](const juce::StringArray& models, bool success) { callback(models, success); });

    auto result = callback.getResult();
    EXPECT_TRUE(std::get<1>(result));
    EXPECT_TRUE(std::get<0>(result).isEmpty());
}

TEST_F(RemoteProviderTest, SetAndGetCurrentModelRoundTripsCosmetically) {
    synth::RemoteProvider provider{kMockHost};
    provider.setModel("whatever-label");
    EXPECT_EQ(provider.getCurrentModel(), juce::String("whatever-label"));
}

TEST_F(RemoteProviderTest, RequestTimeoutMsDefaultsTo240000UnlessChanged) {
    // No regression vs. current (pre-configurability) behavior: a provider that's never had
    // setRequestTimeoutMs() called still uses the same 4-minute ceiling it always has.
    synth::RemoteProvider provider{kMockHost};
    EXPECT_EQ(provider.getRequestTimeoutMs(), 240000);
}

TEST_F(RemoteProviderTest, SetAndGetRequestTimeoutMsRoundTrips) {
    synth::RemoteProvider provider{kMockHost};
    provider.setRequestTimeoutMs(60000);
    EXPECT_EQ(provider.getRequestTimeoutMs(), 60000);
}

TEST_F(RemoteProviderTest, ConfiguredRequestTimeoutMsReachesThePerformHttpCall) {
    // HttpPerformer's signature carries timeoutMs directly (unlike Ollama's InputStreamOptions,
    // there's no need for a capturing static), so this asserts the CONFIGURED instance value
    // reaches performHttp(), not just kDefaultRequestTimeoutMs.
    int capturedTimeoutMs = -1;
    auto performer = [&capturedTimeoutMs](const juce::String&, const juce::StringPairArray&, const juce::String&,
                                          int timeoutMs,
                                          const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedTimeoutMs = timeoutMs;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);
    provider.setRequestTimeoutMs(30000);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedTimeoutMs, 30000);
}
