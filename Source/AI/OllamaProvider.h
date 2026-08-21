#pragma once

#include "AIProvider.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace synth {

/**
 * @class CancellableStream
 * @brief Opt-in mix-in letting cancel() abort a stream that is parked in a blocking read.
 *
 * Real requests do not need this — they are juce::WebInputStreams, which OllamaProvider aborts
 * natively. It exists for injected test factories, so a test double can be unblocked by exactly
 * the mechanism a real socket is, instead of the test having to model cancellation some other way.
 */
struct CancellableStream {
    virtual ~CancellableStream() = default;

    /** Must unblock a concurrent read() promptly. Called from a thread other than the one
        inside read(), and may be called more than once. */
    virtual void cancelRead() = 0;
};

/**
 * @class OllamaProvider
 * @brief AI provider implementation for local Ollama instances.
 */
class OllamaProvider
    : public AIProvider
    , protected juce::Thread {
public:
    /** Hands a stream to the cancellation machinery the moment it exists.

        A factory MUST call this with the stream before doing anything blocking with it, and MUST
        call it with nullptr before destroying a stream it is not going to return. Ownership does
        not transfer; the provider only borrows the pointer, under a lock, for as long as the
        factory says it is alive.

        This exists because of where an Ollama request actually blocks. The request asks for a
        non-streaming response, so the server sends nothing — not even headers — until the whole
        generation is finished, which means connect() is where the entire request is spent.
        juce::URL::createInputStream() connects internally and only then hands back the stream, so
        a factory built on it cannot publish in time and cancel() would have nothing to abort for
        the full generation. Publishing first is what makes cancellation real rather than cosmetic. */
    using StreamPublisher = std::function<void(juce::InputStream*)>;

    using InputStreamFactory = std::function<std::unique_ptr<juce::InputStream>(
        const juce::URL&, const juce::URL::InputStreamOptions&, const StreamPublisher&)>;

    OllamaProvider(const juce::String& host = "http://localhost:11434");

    // Test-specific constructor to inject a mock input stream factory
    OllamaProvider(const juce::String& host, InputStreamFactory streamFactory)
        : Thread("OllamaProviderThread")
        , ollamaHost(host)
        , createStream(std::move(streamFactory)) {}

    ~OllamaProvider() override;

    /** Queues a chat request for the worker thread.

        Contract: every accepted request eventually gets its callback invoked — either
        with the model's answer, or with a typed error. It is never silently dropped,
        including when it lands while the worker is shutting down or already gone.

        Streaming is not implemented here: onDelta is accepted for interface compatibility
        but never invoked.
    */
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema = juce::var(),
                         std::function<void(const juce::String& delta)> onDelta = {}) override;

    /** Abandons a queued or in-flight request.

        The callback still fires exactly once, with AIErrorKind::Cancelled; the request cannot go
        on blocking the ones queued behind it; and an unknown, completed or already-cancelled id
        is a safe no-op. See cancel()'s implementation for how promptly each stream type unblocks. */
    void cancel(RequestId requestId) override;

    juce::String getProviderName() const override { return "Ollama"; }

    using juce::Thread::stopThread; // Make stopThread public for testing purposes

    void setModel(const juce::String& name) override;
    juce::String getCurrentModel() const override;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) override;

    void setTestMode(bool testMode) { isTestMode = testMode; }

    /** Optional knobs for reproducible structured-output requests (P6-13 corruption
        investigation). Unset fields are omitted from the request body entirely, which is
        exactly today's behavior — no production caller sets these, so this is opt-in only,
        used by Tools/AIEvalHarness to pin sampling and to test disabling Ollama's `think`
        (reasoning) mode as a candidate fix for reasoning text leaking into constrained JSON. */
    struct SamplingOptions {
        std::optional<bool> think;
        std::optional<double> temperature;
        std::optional<int> seed;
    };
    void setSamplingOptions(SamplingOptions options) { samplingOptions = options; }

private:
    juce::String ollamaHost;
    juce::String currentModel;
    InputStreamFactory createStream; // Member variable for the stream factory
    bool isTestMode = false;
    SamplingOptions samplingOptions; // all fields unset by default; see setSamplingOptions()
    std::thread discoveryThread;
    // Guards against (a) blocking the caller (we never join on the message thread)
    // and (b) ever having two discovery workers alive at once. Set true before a
    // worker launches, cleared by the worker when it finishes.
    std::atomic<bool> discoveryInFlight{false};

    /** Per-request cancellation state, shared between the queue entry, the registry and the
        worker so cancel() can reach a request wherever it currently is. */
    struct RequestState {
        explicit RequestState(RequestId requestId)
            : id(requestId) {}

        const RequestId id;

        // Set by cancel(). Checked by the worker before it opens a connection and again after the
        // read returns, so a request cancelled at any point is abandoned.
        std::atomic<bool> cancelled{false};

        // Exactly-once gate on the callback. cancel(), the worker and the shutdown path can all
        // race to finish the same request; whoever flips this first owns delivery.
        std::atomic<bool> delivered{false};

        // Non-owning, valid only while the factory or the worker says the stream is alive, so
        // cancel() can abort it. Guarded by streamLock, which is what stops cancel() touching a
        // destroyed stream.
        juce::CriticalSection streamLock;
        juce::InputStream* activeStream = nullptr;
    };

    struct Request {
        RequestId id;
        std::vector<Message> conversation;
        AIProvider::CompletionCallback callback;
        juce::var responseSchema;
        std::shared_ptr<RequestState> state; // null only for a default-constructed Request
    };

    // Monotonically increasing; only ever accessed from sendPrompt() callers, so a plain
    // counter guarded by queueLock (taken for every sendPrompt() anyway) is sufficient.
    uint64_t nextRequestId = 1; // guarded by queueLock

    juce::CriticalSection queueLock;
    std::vector<Request> pendingRequests;

    // Every request that has been accepted and not yet delivered, so cancel() can find one the
    // worker has already popped off pendingRequests. Guarded by queueLock; entries are removed by
    // the delivery path, which is what makes cancel() of a completed id a no-op.
    std::map<uint64_t, std::shared_ptr<RequestState>> inFlight;

    // Who owns the queue. This - NOT isThreadRunning() - is what says "a worker will
    // drain this queue". juce::Thread only clears its handle after run() has already
    // returned, so isThreadRunning() reports true for a worker that has stopped looking
    // at the queue; enqueueing in that window used to strand the request forever with no
    // callback. Ownership is released in the same locked section that empties the queue
    // (see failAllPending), so a concurrent sendPrompt() either hands its request to the
    // retiring worker - which fails it with a callback - or sees `idle` and starts a
    // fresh worker.
    enum class WorkerState {
        idle,     // nobody owns the queue; the next sendPrompt() must start a worker
        starting, // a sendPrompt() has claimed the queue and is starting the thread
        running   // run() has entered and owns the queue
    };
    WorkerState workerState = WorkerState::idle; // guarded by queueLock

    // Set by the destructor before it stops the worker, so a late sendPrompt() fails
    // its caller immediately instead of resurrecting a thread on a dying object.
    bool isShuttingDown = false; // guarded by queueLock

    void run() override;                     // Declaration for inherited method
    void processRequest(const Request& req); // Declaration for private method

    /** Starts a worker for a queue that currently has none. Returns false if no
        worker could be started, in which case the caller MUST fail the queue. */
    bool ensureWorkerRunning();

    /** Releases queue ownership (workerState = idle) and fails everything left in it with
        the given typed error, both in one locked section so nothing can slip in unnoticed
        behind it. */
    void failAllPending(AIErrorKind kind, const juce::String& message);

    /** Single delivery channel for every successful result. `forceSynchronous` bypasses
        MessageManager::callAsync for shutdown paths, where the message loop cannot be
        relied on to run the callback before this object is gone. */
    void deliverResult(const Request& req, const juce::String& responseText, bool forceSynchronous);

    /** Single delivery channel for every error. Same `forceSynchronous` contract as
        deliverResult(). */
    void deliverError(const Request& req, AIErrorKind kind, const juce::String& message, int retryAfterSeconds,
                      bool forceSynchronous);

    /** Claims the sole right to deliver `req`, and deregisters it. Returns false if someone else
        got there first, in which case the caller must not invoke the callback. This is what makes
        "exactly one callback per request" hold when a cancel races a completion. */
    bool claimDelivery(const Request& req);

    /** Aborts the read the given request is parked in, if any. Safe when the request has no
        active stream (already finished, or not started yet). */
    void abortActiveStream(RequestState& state);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OllamaProvider)
};

} // namespace synth
