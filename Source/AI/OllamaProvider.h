#pragma once

#include "AIProvider.h"
#include <atomic>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <vector>

namespace synth {

/**
 * @class OllamaProvider
 * @brief AI provider implementation for local Ollama instances.
 */
class OllamaProvider
    : public AIProvider
    , protected juce::Thread {
public:
    using InputStreamFactory =
        std::function<std::unique_ptr<juce::InputStream>(const juce::URL&, const juce::URL::InputStreamOptions&)>;

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

    /** Cancellation is not implemented for OllamaProvider yet; this is a declared no-op. */
    void cancel(RequestId requestId) override;

    juce::String getProviderName() const override { return "Ollama"; }

    using juce::Thread::stopThread; // Make stopThread public for testing purposes

    void setModel(const juce::String& name) override;
    juce::String getCurrentModel() const override;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) override;

    void setTestMode(bool testMode) { isTestMode = testMode; }

private:
    juce::String ollamaHost;
    juce::String currentModel;
    InputStreamFactory createStream; // Member variable for the stream factory
    bool isTestMode = false;
    std::thread discoveryThread;
    // Guards against (a) blocking the caller (we never join on the message thread)
    // and (b) ever having two discovery workers alive at once. Set true before a
    // worker launches, cleared by the worker when it finishes.
    std::atomic<bool> discoveryInFlight{false};

    struct Request {
        RequestId id;
        std::vector<Message> conversation;
        AIProvider::CompletionCallback callback;
        juce::var responseSchema;
    };

    // Monotonically increasing; only ever accessed from sendPrompt() callers, so a plain
    // counter guarded by queueLock (taken for every sendPrompt() anyway) is sufficient.
    uint64_t nextRequestId = 1; // guarded by queueLock

    juce::CriticalSection queueLock;
    std::vector<Request> pendingRequests;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OllamaProvider)
};

} // namespace synth
