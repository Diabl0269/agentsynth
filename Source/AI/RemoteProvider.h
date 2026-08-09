#pragma once

#include "AIProvider.h"
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <map>
#include <memory>
#include <vector>

namespace synth {

/**
 * @class RemoteProvider
 * @brief AIProvider implementation talking to a local instance of the synth-platform inference
 *        service (`POST {host}/v1/capability/patch.generate`) over libcurl.
 *
 * Mirrors OllamaProvider's worker-thread/queue/cancellation architecture (see that class for the
 * full rationale — the machinery here is deliberately the same shape, just simplified where
 * libcurl makes cancellation easier than juce::WebInputStream does): a queueLock-guarded
 * pendingRequests vector, a per-request RequestState shared between the queue and cancel(), an
 * idle/starting/running worker-state machine, and an exactly-once claimDelivery()/deliverResult()/
 * deliverError() pair with the same forceSynchronous-during-shutdown contract.
 *
 * Non-streaming: onDelta is accepted for interface compatibility but never invoked, because the
 * service's patch.generate endpoint is not streaming-capable (a single `c.json({data})`).
 *
 * Ships registered in AIProviderRegistry but hidden from the Settings UI
 * (ProviderDescriptor::hidden) until a later phase turns it on.
 */
class RemoteProvider
    : public AIProvider
    , protected juce::Thread {
public:
    /** Result of one HTTP transaction, transport-layer only — no interpretation of the body. */
    struct HttpResult {
        int httpStatus = 0;
        juce::String body;
        juce::StringPairArray headers;
        bool transportFailed = false; // curl couldn't complete the transfer at all (DNS/connect/etc.)
        bool timedOut = false;
        juce::String errorMessage; // human-readable, meaningful when transportFailed or timedOut
    };

    /** Performs one HTTP POST. `cancelled` is polled by the implementation (real or fake) so a
        request can be aborted mid-transfer; see cancel()'s doc comment for the full contract. */
    using HttpPerformer =
        std::function<HttpResult(const juce::String& url, const juce::StringPairArray& requestHeaders,
                                 const juce::String& jsonBody, int timeoutMs, const std::atomic<bool>& cancelled)>;

    RemoteProvider(const juce::String& host = "http://localhost:8787");

    // Test-specific constructor to inject a fake HTTP transport (no real sockets). `deviceId`
    // defaults to empty (X-Device-Id header omitted) so existing 2-arg call sites keep compiling
    // and never touch the real per-install DeviceIdStore file.
    RemoteProvider(const juce::String& host, HttpPerformer performer, juce::String deviceId = "");

    ~RemoteProvider() override;

    /** Queues a structured patch-generation request for the worker thread.

        Contract: every accepted request eventually gets its callback invoked — either with the
        service's answer, or with a typed error. It is never silently dropped, including when it
        lands while the worker is shutting down or already gone.

        RemoteProvider only supports structured patch generation (see AIErrorKind::Schema fail-fast
        below): the service currently exposes no conversational capability. Streaming is not
        implemented: onDelta is accepted for interface compatibility but never invoked.
    */
    RequestId sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema = juce::var(),
                         std::function<void(const juce::String& delta)> onDelta = {}) override;

    /** Abandons a queued or in-flight request. See OllamaProvider::cancel() for the full contract
        this mirrors: exactly-once callback, no-op on an unknown/completed id, and a queued request
        is pulled out and delivered Cancelled immediately rather than left to the worker. */
    void cancel(RequestId requestId) override;

    juce::String getProviderName() const override { return "Remote"; }

    using juce::Thread::stopThread; // Make stopThread public for testing purposes

    void setModel(const juce::String& name) override;
    juce::String getCurrentModel() const override;
    void fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) override;

    /** Stores the token; when non-empty, every request carries `Authorization: Bearer <token>`. */
    void setAuthToken(const juce::String& token) override;

    void setTestMode(bool testMode) { isTestMode = testMode; }

private:
    juce::String remoteHost;
    juce::String currentModel; // cosmetic only: the service picks its own model server-side
    juce::String authToken;
    // A stable per-install identifier (Source/Auth/DeviceIdStore.h), sent as the X-Device-Id
    // header on every capability request whether or not authToken is set — used for an anonymous
    // free-trial tier when there's no bearer token, and as anti-abuse signal once there is one.
    // NOT a secret. Populated from DeviceIdStore in the production constructor; empty (header
    // omitted) for the test constructor, mirroring authToken's default-empty/opt-in shape.
    juce::String deviceId;
    HttpPerformer performHttp;
    bool isTestMode = false;

    /** Per-request cancellation state, shared between the queue entry, the registry and the
        worker so cancel() can reach a request wherever it currently is.

        Simpler than OllamaProvider::RequestState: libcurl's CURLOPT_XFERINFOFUNCTION progress
        callback polls `cancelled` directly from inside curl_easy_perform() on the worker thread,
        so there is no separate stream pointer/lock to publish — the atomic alone is the whole
        cancellation channel. */
    struct RequestState {
        explicit RequestState(RequestId requestId)
            : id(requestId) {}

        const RequestId id;

        // Set by cancel(). Polled by the HTTP performer's progress callback during the transfer,
        // and checked by the worker before and after the call, so a request cancelled at any
        // point is abandoned.
        std::atomic<bool> cancelled{false};

        // Exactly-once gate on the callback. cancel(), the worker and the shutdown path can all
        // race to finish the same request; whoever flips this first owns delivery.
        std::atomic<bool> delivered{false};
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

    // Who owns the queue — see OllamaProvider::WorkerState for the full rationale (identical
    // here): NOT isThreadRunning(), because juce::Thread only clears its handle after run() has
    // already returned.
    enum class WorkerState { idle, starting, running };
    WorkerState workerState = WorkerState::idle; // guarded by queueLock

    // Set by the destructor before it stops the worker, so a late sendPrompt() fails its caller
    // immediately instead of resurrecting a thread on a dying object.
    bool isShuttingDown = false; // guarded by queueLock

    void run() override;
    void processRequest(const Request& req);

    /** Starts a worker for a queue that currently has none. Returns false if no worker could be
        started, in which case the caller MUST fail the queue. */
    bool ensureWorkerRunning();

    /** Releases queue ownership (workerState = idle) and fails everything left in it with the
        given typed error, both in one locked section so nothing can slip in unnoticed behind it. */
    void failAllPending(AIErrorKind kind, const juce::String& message);

    /** Single delivery channel for every successful result. `forceSynchronous` bypasses
        MessageManager::callAsync for shutdown paths, where the message loop cannot be relied on to
        run the callback before this object is gone. */
    void deliverResult(const Request& req, const juce::String& responseText, bool forceSynchronous);

    /** Single delivery channel for every error. Same `forceSynchronous` contract as
        deliverResult(). */
    void deliverError(const Request& req, AIErrorKind kind, const juce::String& message, int retryAfterSeconds,
                      bool forceSynchronous);

    /** Claims the sole right to deliver `req`, and deregisters it. Returns false if someone else
        got there first, in which case the caller must not invoke the callback. */
    bool claimDelivery(const Request& req);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RemoteProvider)
};

} // namespace synth
