// Cancellation
//
// The Cancel button used to be cosmetic: it hid the spinner while the HTTP request ran to
// completion, so a metered backend still billed for the abandoned work and the serial request
// queue still made the user's next message wait behind it. These lock the real behaviour in.
#include "OllamaProviderTestFixture.h"

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
