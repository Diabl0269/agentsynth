// Worker-thread lifecycle: queued requests surviving a shutdown race or destruction, and the
// slow-stream timeout path.
#include "OllamaProviderTestFixture.h"

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
