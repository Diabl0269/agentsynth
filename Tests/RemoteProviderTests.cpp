#include "../Source/AI/RemoteProvider.h"
#include "../Source/Branding.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <future>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <memory>
#include <mutex>

namespace {

struct MockCompletionCallback {
    std::promise<std::pair<juce::String, bool>> promise;

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

// Bounded-wait latch for "did the callback fire?" assertions. See OllamaProviderTests.cpp for the
// identical rationale: every wait has a timeout so a lost request fails the test instead of
// hanging it.
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

const juce::String kMockHost = "http://mock-host:8787";

// A non-void response schema, matching what AIIntegrationService::sendMessage() passes for a
// structured patch request (AIStateMapper::getPatchSchema()). Content is irrelevant to
// RemoteProvider — it never sends "format" the way OllamaProvider does — only isVoid() matters.
juce::var makeSchema() { return juce::var(new juce::DynamicObject()); }

synth::RemoteProvider::HttpResult makeSuccess(const juce::String& body) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = 200;
    result.body = body;
    return result;
}

synth::RemoteProvider::HttpResult makeStatus(int status, const juce::String& body = "{}",
                                             const juce::StringPairArray& headers = {}) {
    synth::RemoteProvider::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    result.headers = headers;
    return result;
}

} // namespace

class RemoteProviderTest : public ::testing::Test {
protected:
    void TearDown() override {
        // Nothing persistent between tests: each test constructs its own provider.
    }
};

// ============================================================================
// Request shape
// ============================================================================

TEST_F(RemoteProviderTest, RequestShapePlainTextConversation) {
    juce::String capturedUrl;
    juce::StringPairArray capturedHeaders;
    juce::String capturedBody;

    auto performer = [&](const juce::String& url, const juce::StringPairArray& headers, const juce::String& jsonBody,
                         int, const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedUrl = url;
        capturedHeaders = headers;
        capturedBody = jsonBody;
        return makeSuccess(R"({"data":{"nodes":[]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::vector<synth::AIProvider::Message> conversation = {{"user", "Make a bass patch"}};
    MockPromptCallback callback;
    provider.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    EXPECT_EQ(capturedUrl, kMockHost + "/v1/capability/patch.generate");
    EXPECT_TRUE(capturedHeaders.containsKey("Content-Type"));
    EXPECT_EQ(capturedHeaders.getValue("Content-Type", ""), juce::String("application/json"));
    EXPECT_FALSE(capturedHeaders.containsKey("Authorization"));

    juce::var parsedBody = juce::JSON::parse(capturedBody);
    ASSERT_TRUE(parsedBody.isObject());
    auto* obj = parsedBody.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->getProperty("productName").toString(), juce::String(synth::branding::kProductName));
    EXPECT_EQ(obj->getProperty("userPrompt").toString(), juce::String("Make a bass patch"));
    EXPECT_FALSE(obj->hasProperty("currentPatch"));
    EXPECT_FALSE(obj->hasProperty("promptVersion"));
}

// The design hinges on this: AIIntegrationService::buildPatchAugmentedContent() wraps the current
// graph into the last message as "Current patch state:...User request: ...", and that wrapper
// must pass through into userPrompt completely unchanged, never re-parsed or re-split.
TEST_F(RemoteProviderTest, RequestShapePassesAugmentedContentThroughUnchanged) {
    const juce::String wrapped =
        "Current patch state:\n```json\n{\"nodes\":[{\"id\":1}]}\n```\n\nUser request: add a filter";

    juce::String capturedBody;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String& jsonBody, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedBody = jsonBody;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    std::vector<synth::AIProvider::Message> conversation = {{"user", wrapped}};
    MockPromptCallback callback;
    provider.sendPrompt(
        conversation, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    juce::var parsedBody = juce::JSON::parse(capturedBody);
    auto* obj = parsedBody.getDynamicObject();
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(obj->getProperty("userPrompt").toString(), wrapped);
    EXPECT_FALSE(obj->hasProperty("currentPatch"));
}

TEST_F(RemoteProviderTest, AuthorizationHeaderOnlySentWhenTokenSet) {
    juce::StringPairArray capturedHeaders;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        capturedHeaders = headers;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);
    provider.setAuthToken("secret-token-123");

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    callback.getResult();
    provider.stopThread(5000);

    EXPECT_TRUE(capturedHeaders.containsKey("Authorization"));
    EXPECT_EQ(capturedHeaders.getValue("Authorization", ""), juce::String("Bearer secret-token-123"));
}

// ============================================================================
// Fail-fast: no network call
// ============================================================================

TEST_F(RemoteProviderTest, VoidResponseSchemaFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    // No schema argument: defaults to juce::var() (void), exactly what AIIntegrationService::
    // sendMessage() passes for a plain conversational turn.
    provider.sendPrompt({{"user", "hello"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); });

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

TEST_F(RemoteProviderTest, EmptyConversationFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt({}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

TEST_F(RemoteProviderTest, BlankLastMessageFailsFastWithoutHittingNetwork) {
    bool performerInvoked = false;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        performerInvoked = true;
        return makeSuccess(R"({"data":{}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "   "}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
    EXPECT_FALSE(performerInvoked);
}

// ============================================================================
// HTTP status -> AIErrorKind mapping
// ============================================================================

struct StatusMappingCase {
    int httpStatus;
    synth::AIProvider::AIErrorKind expectedKind;
};

class RemoteProviderStatusMappingTest : public ::testing::TestWithParam<StatusMappingCase> {};

TEST_P(RemoteProviderStatusMappingTest, MapsStatusToExpectedErrorKind) {
    const auto testCase = GetParam();

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(testCase.httpStatus, "{}");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, testCase.expectedKind) << "for HTTP " << testCase.httpStatus;
}

INSTANTIATE_TEST_SUITE_P(HttpStatuses, RemoteProviderStatusMappingTest,
                         ::testing::Values(StatusMappingCase{401, synth::AIProvider::AIErrorKind::Auth},
                                           StatusMappingCase{403, synth::AIProvider::AIErrorKind::Auth},
                                           StatusMappingCase{402, synth::AIProvider::AIErrorKind::Quota},
                                           StatusMappingCase{400, synth::AIProvider::AIErrorKind::Schema},
                                           StatusMappingCase{404, synth::AIProvider::AIErrorKind::Schema},
                                           StatusMappingCase{500, synth::AIProvider::AIErrorKind::Server},
                                           StatusMappingCase{502, synth::AIProvider::AIErrorKind::Server},
                                           StatusMappingCase{418, synth::AIProvider::AIErrorKind::Server}));

TEST_F(RemoteProviderTest, MapsTooManyRequestsToRateLimitAndReadsRetryAfter) {
    juce::StringPairArray headers;
    headers.set("Retry-After", "45");

    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(429, "{}", headers);
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::RateLimit);
    EXPECT_EQ(result.error.retryAfterSeconds, 45);
}

// ============================================================================
// Success / malformed bodies
// ============================================================================

TEST_F(RemoteProviderTest, SuccessReturnsDataReserializedAsJsonText) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"data":{"nodes":[{"id":1,"type":"Oscillator"}]}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    ASSERT_TRUE(result.success);
    juce::var parsedContent = juce::JSON::parse(result.content);
    ASSERT_TRUE(parsedContent.isObject());
    auto* nodes = parsedContent.getProperty("nodes", juce::var()).getArray();
    ASSERT_NE(nodes, nullptr);
    ASSERT_EQ(nodes->size(), 1);
    EXPECT_EQ((*nodes)[0].getProperty("type", juce::var()).toString(), juce::String("Oscillator"));
}

TEST_F(RemoteProviderTest, UnparseableSuccessBodyMapsToSchema) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess("not json at all");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

TEST_F(RemoteProviderTest, SuccessBodyWithNoDataKeyMapsToSchema) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeSuccess(R"({"foo":1})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Schema);
}

TEST_F(RemoteProviderTest, ErrorBodyMessageIsAppendedToDeliveredError) {
    auto performer = [](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                        const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        return makeStatus(502, R"({"error":{"code":"GENERATION_FAILED","message":"model timed out mid-generation"}})");
    };

    synth::RemoteProvider provider{kMockHost, performer};
    provider.setTestMode(true);

    MockPromptCallback callback;
    provider.sendPrompt(
        {{"user", "hi"}}, [&callback](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());

    auto result = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(result.success);
    EXPECT_EQ(result.error.kind, synth::AIProvider::AIErrorKind::Server);
    EXPECT_TRUE(result.error.message.contains("model timed out mid-generation"));
}

// ============================================================================
// Transport-level failures
// ============================================================================

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

// ============================================================================
// Cancellation
// ============================================================================

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

// ============================================================================
// Misc interface conformance
// ============================================================================

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
