#include "OllamaProvider.h"
#include <algorithm>

namespace synth {

namespace {
// How long run() parks when the queue is empty. The wait is woken immediately by
// sendPrompt()'s notify() and by stopThread() (which notifies the same event), so
// this bound is purely a lost-wakeup safety net — never a poll we rely on.
constexpr int kIdleWaitMs = 1000;

// Upper bound on waiting for a retired worker's thread handle to be released so a
// replacement can start. The previous run() has already returned by then, so in
// practice this costs JUCE's ~2 ms teardown poll.
constexpr int kWorkerHandoverTimeoutMs = 2000;

const char* const kShuttingDownMessage = "Error: Request cancelled - the Ollama provider is shutting down.";

// Delivered with AIErrorKind::Cancelled. Callers are expected to branch on the kind rather than
// show this text; it exists so a caller that ignores the kind still sees something sane.
const char* const kCancelledMessage = "Request cancelled.";

/** Unblocks a stream that is parked in a blocking read — or in the connect that precedes it.

    juce::WebInputStream::cancel() is the real mechanism: documented to "cancel a blocking read and
    prevent any subsequent connection attempts", and meant to be called from another thread. That
    second half matters as much as the first here, because an Ollama chat request is non-streaming
    and therefore spends nearly all its time inside connect() waiting for the server to finish
    generating — see StreamPublisher for why the stream is published to us before it is connected.

    CancellableStream is the escape hatch for injected test factories, which are not
    WebInputStreams.

    Anything else falls back to the cancelled flag alone: the worker finishes the read and throws
    the response away. Still correct — the callback fires with Cancelled and the queue moves on —
    just not prompt. */
void abortStream(juce::InputStream& stream) {
    if (auto* web = dynamic_cast<juce::WebInputStream*>(&stream)) {
        web->cancel();
        return;
    }

    if (auto* cancellable = dynamic_cast<CancellableStream*>(&stream))
        cancellable->cancelRead();
}
} // namespace

using AIErrorKind = OllamaProvider::AIErrorKind;

// Default constructor implementation
OllamaProvider::OllamaProvider(const juce::String& host)
    : Thread("OllamaProviderThread")
    , ollamaHost(host)
    , createStream([](const juce::URL& url, const juce::URL::InputStreamOptions& options,
                      const StreamPublisher& publish) -> std::unique_ptr<juce::InputStream> {
        // Deliberately NOT juce::URL::createInputStream(). That helper connects internally and
        // only hands the stream back afterwards, which is useless to us: an Ollama chat request
        // is non-streaming, so the server sends nothing — not even headers — until the generation
        // is complete, and connect() is where the whole request is spent. Building the
        // WebInputStream here lets us publish it for cancellation BEFORE connecting, which is the
        // difference between Cancel aborting the request and Cancel just hiding the spinner.
        //
        // Verified against a live Ollama: publishing after connect leaves cancel() unable to stop
        // a long generation at all; publishing before it frees the worker in 1-2 ms.
        const bool usePost = options.getParameterHandling() == juce::URL::ParameterHandling::inPostData;
        auto stream = std::make_unique<juce::WebInputStream>(url, usePost);

        if (const auto extraHeaders = options.getExtraHeaders(); extraHeaders.isNotEmpty())
            stream->withExtraHeaders(extraHeaders);

        if (const auto timeout = options.getConnectionTimeoutMs(); timeout != 0)
            stream->withConnectionTimeout(timeout);

        if (const auto requestCmd = options.getHttpRequestCmd(); requestCmd.isNotEmpty())
            stream->withCustomRequestCommand(requestCmd);

        stream->withNumRedirectsToFollow(options.getNumRedirectsToFollow());

        publish(stream.get());

        const bool connected = stream->connect(nullptr);

        if (auto* status = options.getStatusCode())
            *status = stream->getStatusCode();

        if (auto* responseHeaders = options.getResponseHeaders())
            *responseHeaders = stream->getResponseHeaders();

        if (!connected || stream->isError()) {
            // Retract before destroying it, so a concurrent cancel() cannot reach a dead stream.
            publish(nullptr);
            return nullptr;
        }

        return stream;
    }) {}

OllamaProvider::~OllamaProvider() {
    {
        const juce::ScopedLock sl(queueLock);
        isShuttingDown = true;
    }

    // Wakes run() (stopThread signals the exit flag and notify()s the thread's event),
    // then blocks until run() has returned. run() retires through failAllPending(),
    // which delivers the shutdown error for every still-queued request INLINE — see
    // failAllPending() for why that delivery must not be deferred here.
    stopThread(2000);

    // Belt and braces: covers the case where no worker ever ran (so nothing retired)
    // and anything that slipped into the queue during teardown.
    failAllPending(AIErrorKind::Cancelled, kShuttingDownMessage);

    // Join the discovery worker so it cannot touch our members after we are
    // destroyed (it captures `this`). With the short 4s connection timeout
    // and the in-flight guard ensuring at most ONE worker exists, this join
    // can block for at most ~4s in the worst case (server unreachable), never
    // the old 120s. We never reassign a joinable thread (the worker is only
    // launched when none is in flight), so std::terminate cannot fire here.
    if (discoveryThread.joinable())
        discoveryThread.join();
}

OllamaProvider::RequestId OllamaProvider::sendPrompt(const std::vector<Message>& conversation,
                                                     CompletionCallback callback, const juce::var& responseSchema,
                                                     std::function<void(const juce::String&)> onDelta) {
    // Streaming is not implemented for OllamaProvider: onDelta is accepted for interface
    // compatibility but intentionally never invoked.
    juce::ignoreUnused(onDelta);

    Request request{RequestId{}, conversation, std::move(callback), responseSchema, nullptr};
    bool rejected = false;
    bool mustStartWorker = false;

    {
        const juce::ScopedLock sl(queueLock);

        request.id.value = nextRequestId++;
        request.state = std::make_shared<RequestState>(request.id);

        if (isShuttingDown) {
            rejected = true;
        } else {
            // Registered under the same lock as the queue push, so a cancel() racing this call
            // either does not see the request at all or sees it in both places.
            inFlight.emplace(request.id.value, request.state);
            pendingRequests.push_back(request);

            // Self-heal: `running` plus a dead thread means run() entered but ended
            // without releasing ownership - juce::Thread force-kills the worker when
            // stopThread() is given a zero timeout, and a throwing run() ends the same
            // way. Nobody is left to drain the queue, so take it over instead of waiting
            // forever. There is no false positive here: a worker that returns normally
            // sets `idle` before juce clears its handle.
            //
            // Deliberately untested: reproducing it needs a force-killed thread, and
            // pthread_cancel's forced unwind aborts under glibc when it reaches the
            // `catch (...)` in juce's threadEntryPoint. Kept anyway because without it
            // this path would be a regression - the old code recovered here (a killed
            // thread clears the handle, so its isThreadRunning() check restarted).
            const bool ownerVanished = (workerState == WorkerState::running && !isThreadRunning());
            mustStartWorker = (workerState == WorkerState::idle) || ownerVanished;

            // Claim ownership now, under the same lock as the push, so exactly one
            // caller per retirement takes on starting the worker.
            if (mustStartWorker)
                workerState = WorkerState::starting;
        }
    }

    const RequestId requestId = request.id;

    if (rejected) {
        deliverError(request, AIErrorKind::Cancelled, kShuttingDownMessage, /*retryAfterSeconds=*/0,
                     /*forceSynchronous=*/true);
        return requestId;
    }

    if (mustStartWorker && !ensureWorkerRunning()) {
        // No worker could take the queue. Failing loudly beats the UI waiting forever.
        failAllPending(AIErrorKind::Server, "Error: Could not start the Ollama request worker thread.");
        return requestId;
    }

    // Wake a worker parked in run()'s wait(). Harmless if it is busy or just started:
    // the event is auto-reset and every loop iteration re-checks the queue under lock,
    // so a stale signal only costs one extra iteration and a wakeup is never lost.
    notify();

    return requestId;
}

void OllamaProvider::cancel(RequestId requestId) {
    if (requestId.value == 0)
        return;

    std::shared_ptr<RequestState> state;
    Request abandoned;
    bool tookFromQueue = false;

    {
        const juce::ScopedLock sl(queueLock);

        const auto entry = inFlight.find(requestId.value);
        if (entry == inFlight.end())
            return; // Unknown id, or the request already completed and was delivered: nothing to do.

        state = entry->second;
        state->cancelled.store(true);

        // If the worker has not picked this up yet we own it outright. Taking it out of the queue
        // here — under the same lock the worker pops with, so exactly one of us ends up holding
        // it — is what stops an abandoned request delaying the ones behind it.
        const auto queued = std::find_if(pendingRequests.begin(), pendingRequests.end(),
                                         [&state](const Request& r) { return r.state == state; });

        if (queued != pendingRequests.end()) {
            abandoned = *queued;
            pendingRequests.erase(queued);
            tookFromQueue = true;
        }
    }

    if (tookFromQueue) {
        // Delivered outside the lock: in test mode the callback runs inline, and one that
        // re-entered sendPrompt() while we held queueLock would be a surprise at best.
        deliverError(abandoned, AIErrorKind::Cancelled, kCancelledMessage, /*retryAfterSeconds=*/0,
                     /*forceSynchronous=*/false);
        return;
    }

    // Already in flight. Unblock the read (or the connect it is still inside); the worker sees the
    // cancelled flag on the way out and delivers the Cancelled callback itself. Leaving delivery to
    // the worker keeps a single owner for the request and means it is genuinely free before we
    // return, rather than still holding the queue behind a socket nobody is waiting on.
    abortActiveStream(*state);
}

void OllamaProvider::abortActiveStream(RequestState& state) {
    const juce::ScopedLock sl(state.streamLock);
    if (state.activeStream != nullptr)
        abortStream(*state.activeStream);
}

bool OllamaProvider::claimDelivery(const Request& req) {
    if (req.state == nullptr)
        return true; // Default-constructed Request (no state): nothing to arbitrate.

    // cancel(), the worker and failAllPending() can all reach the same request, and cancelling one
    // that is completing right now makes that a real race. Whoever wins the exchange owns the
    // callback; everyone else backs off having done nothing.
    if (req.state->delivered.exchange(true))
        return false;

    // Deregister before the caller calls out, so a cancel() issued from inside the callback (or
    // from another thread the instant it fires) sees a completed id and no-ops instead of trying
    // to abort a stream that is on its way out.
    const juce::ScopedLock sl(queueLock);
    inFlight.erase(req.state->id.value);
    return true;
}

bool OllamaProvider::ensureWorkerRunning() {
    if (startThread())
        return true;

    // Never wait for ourselves. A callback delivered inline on the worker thread can
    // re-enter sendPrompt() while that same thread is retiring; waitForThreadToExit()
    // would then stall for the full timeout (and trip a juce assertion). Report failure
    // so the caller fails the request with a callback instead of hanging.
    if (juce::Thread::getCurrentThreadId() == getThreadId())
        return false;

    // startThread() is a silent no-op while juce::Thread still holds a thread handle.
    // We only get here having just claimed an unowned queue, which means the previous
    // run() has already returned and juce simply has not released the handle yet - the
    // exact window in which requests used to vanish. Wait for the handle, then start a
    // genuinely fresh worker.
    if (!waitForThreadToExit(kWorkerHandoverTimeoutMs))
        return false;

    return startThread();
}

void OllamaProvider::failAllPending(AIErrorKind kind, const juce::String& message) {
    std::vector<Request> stranded;
    bool deliverSynchronously = false;

    {
        const juce::ScopedLock sl(queueLock);
        stranded.swap(pendingRequests);
        workerState = WorkerState::idle;
        // During destruction the message loop cannot be trusted to run a callAsync
        // before this object — and whatever the callback captured — is gone. Deliver
        // inline instead, while the destructor is still blocked inside stopThread().
        // That is the only way "queued requests are failed with a callback" and "no
        // callback fires after destruction" can both hold.
        deliverSynchronously = isShuttingDown;
    }

    for (const auto& req : stranded)
        deliverError(req, kind, message, /*retryAfterSeconds=*/0, deliverSynchronously);
}

void OllamaProvider::deliverResult(const Request& req, const juce::String& responseText, bool forceSynchronous) {
    // Claimed before the callback check, so a request with no callback is still deregistered and
    // a later cancel() of its id stays a no-op.
    if (!claimDelivery(req))
        return;

    if (!req.callback)
        return;

    AIResponse response;
    response.success = true;
    response.content = responseText;
    response.requestId = juce::String(req.id.value);

    if (isTestMode || forceSynchronous) {
        req.callback(response);
        return;
    }

    // Copy just the callback (not the whole conversation) and never `this`, so a
    // message already in flight cannot touch a destroyed provider.
    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, response]() { callback(response); });
}

void OllamaProvider::deliverError(const Request& req, AIErrorKind kind, const juce::String& message,
                                  int retryAfterSeconds, bool forceSynchronous) {
    if (!claimDelivery(req))
        return;

    if (!req.callback)
        return;

    AIResponse response;
    response.success = false;
    response.requestId = juce::String(req.id.value);
    response.error.kind = kind;
    response.error.message = message;
    response.error.retryAfterSeconds = retryAfterSeconds;
    response.error.requestId = response.requestId;

    if (isTestMode || forceSynchronous) {
        req.callback(response);
        return;
    }

    // Copy just the callback (not the whole conversation) and never `this`, so a
    // message already in flight cannot touch a destroyed provider.
    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, response]() { callback(response); });
}

void OllamaProvider::setModel(const juce::String& name) { currentModel = name; }
juce::String OllamaProvider::getCurrentModel() const { return currentModel; }

void OllamaProvider::fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) {
    // Run on a separate thread to avoid blocking the caller (the message thread).
    //
    // CRITICAL: we must NOT join() on the caller's thread. The old code joined the
    // previous discovery thread here, which blocked the UI for up to the connection
    // timeout (was 120s) when Ollama was unreachable. Instead we guard with an
    // atomic in-flight flag: if a discovery is already running, this call returns
    // immediately (and reports failure asynchronously) without ever blocking.
    bool expected = false;
    if (!discoveryInFlight.compare_exchange_strong(expected, true)) {
        // A discovery worker is already running. Do not block the caller and do not
        // launch a second worker. Report "no result yet" asynchronously so callers
        // relying on the callback still get one without us touching the UI synchronously.
        if (callback) {
            if (isTestMode) {
                callback(juce::StringArray{}, false);
            } else {
                juce::MessageManager::callAsync([callback]() { callback(juce::StringArray{}, false); });
            }
        }
        return;
    }

    // No worker was in flight. A previous worker may have finished but not yet been
    // joined; joining a finished thread returns immediately (no blocking). We only
    // ever reach here when discoveryInFlight just transitioned false->true, so there
    // is no concurrent worker to wait on.
    if (discoveryThread.joinable())
        discoveryThread.join();

    discoveryThread = std::thread([this, callback]() {
        // Clear the in-flight flag when this worker exits, no matter which path it
        // takes, so a subsequent fetch can run.
        struct InFlightGuard {
            std::atomic<bool>& flag;
            ~InFlightGuard() { flag.store(false); }
        } inFlightGuard{discoveryInFlight};

        DBG("AI Discovery STARTED: " + ollamaHost + "/api/tags");
        juce::URL url(ollamaHost + "/api/tags");
        juce::StringArray models;
        bool success = false;

        // Model discovery is not cancellable (bounded by its own 4 s connection timeout and with
        // no user-visible Cancel), so it publishes nothing.
        if (auto stream = createStream(
                url,
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(4000),
                [](juce::InputStream*) {})) {
            juce::String responseText = stream->readEntireStreamAsString();
            // DBG("AI Discovery Response (before JSON parse): [" + responseText + "]"); // Potentially expensive
            // DBG("AI Discovery Response: " + responseText); // Redundant

            juce::var jsonResponse = juce::JSON::parse(responseText);

            DBG("AI Discovery Debug: jsonResponse.isObject() = " +
                juce::String(jsonResponse.isObject() ? "true" : "false"));
            if (jsonResponse.isObject()) {
                DBG("AI Discovery Debug: hasProperty(\"models\") = " +
                    juce::String(jsonResponse.getDynamicObject()->hasProperty("models") ? "true" : "false"));
                if (jsonResponse.getDynamicObject()->hasProperty("models")) {
                    auto modelsArray = jsonResponse["models"];
                    DBG("AI Discovery Debug: modelsArray.isArray() = " +
                        juce::String(modelsArray.isArray() ? "true" : "false"));
                    if (modelsArray.isArray()) {
                        for (int i = 0; i < modelsArray.size(); ++i) {
                            models.add(modelsArray[i]["name"].toString());
                        }
                        success = true;
                        DBG("AI Discovery: Found " + juce::String(models.size()) + " " +
                            (models.size() == 1 ? "model" : "models"));
                    } else {
                        DBG("AI Discovery Error: 'models' property is not an array");
                    }
                } else {
                    DBG("AI Discovery Error: JSON response does not have 'models' property");
                }
            } else {
                DBG("AI Discovery Error: JSON response is not an object");
            }
        } else {
            DBG("AI Discovery Error: Failed to open input stream for " + url.toString(true));
        }

        if (isTestMode) {
            if (callback)
                callback(models, success);
        } else {
            juce::MessageManager::callAsync([callback, models, success]() {
                if (callback)
                    callback(models, success);
            });
        }
    });
}

void OllamaProvider::run() {
    // The worker parks on an empty queue instead of exiting. Exiting on drain was the
    // request-loss race: run() would break out while isThreadRunning() still reported
    // true, so sendPrompt() skipped startThread() and the request sat in the queue
    // forever with no callback. The only way out of this loop now is threadShouldExit().
    {
        const juce::ScopedLock sl(queueLock);
        workerState = WorkerState::running;
    }

    for (;;) {
        Request currentRequest;
        bool haveRequest = false;

        {
            const juce::ScopedLock sl(queueLock);

            if (threadShouldExit())
                break; // leave the queue intact; failAllPending() below reports it

            if (!pendingRequests.empty()) {
                currentRequest = pendingRequests.front();
                pendingRequests.erase(pendingRequests.begin());
                haveRequest = true;
            }
        }

        if (haveRequest) {
            processRequest(currentRequest);
            continue;
        }

        wait(kIdleWaitMs);
    }

    // Retire: hand the queue back and fail anything still in it, atomically, so no
    // request can slip in behind us unnoticed.
    failAllPending(AIErrorKind::Cancelled, kShuttingDownMessage);
}

void OllamaProvider::processRequest(const Request& req) {
    // Delivers a success/error through the same test-mode-aware channel used by every
    // exit path below, so the fail-fast path and the network paths stay identical.
    auto deliverSuccess = [this, &req](const juce::String& responseText) {
        deliverResult(req, responseText, /*forceSynchronous=*/false);
    };
    auto deliverErr = [this, &req](AIErrorKind kind, const juce::String& message, int retryAfterSeconds = 0) {
        deliverError(req, kind, message, retryAfterSeconds, /*forceSynchronous=*/false);
    };

    // Fail fast: never hit the network with an empty model. Ollama rejects such requests
    // with HTTP 400 "model is required", which previously surfaced as a misleading
    // "Could not connect to Ollama" error even when the server was reachable. This is a
    // client-side precondition, so it maps to Schema rather than Network.
    if (currentModel.isEmpty()) {
        deliverErr(AIErrorKind::Schema,
                   "Error: No Ollama model selected. Check that Ollama is running and that a model is available "
                   "(ollama list).");
        return;
    }

    auto wasCancelled = [&req] { return req.state != nullptr && req.state->cancelled.load(); };

    // Cancelled while it sat in the queue, or during the handoff to this thread. Bail before
    // opening a connection — the whole point is not to spend money on abandoned work.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    juce::URL url(ollamaHost + "/api/chat");

    // Build JSON body
    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("model", currentModel);
    body->setProperty("stream", false);

    juce::Array<juce::var> messages;
    for (const auto& msg : req.conversation) {
        juce::DynamicObject::Ptr m = new juce::DynamicObject();
        m->setProperty("role", msg.role);
        m->setProperty("content", msg.content);
        messages.add(juce::var(m.get()));
    }
    body->setProperty("messages", messages);

    if (!req.responseSchema.isVoid())
        body->setProperty("format", req.responseSchema);

    juce::String jsonString = juce::JSON::toString(juce::var(body.get()));

    int httpStatus = 0;
    juce::StringPairArray responseHeaders;

    // Declared before the guard below so it is destroyed AFTER it: the guard must have already
    // cleared activeStream by the time the stream itself goes away, or cancel() could reach a
    // destroyed object.
    std::unique_ptr<juce::InputStream> stream;

    // Retracts the published pointer however this scope is left. Every store and load of
    // activeStream is under streamLock, which is the only thing standing between cancel() and a
    // dangling pointer. streamLock is never held while taking queueLock, so the two cannot
    // deadlock against each other.
    struct ScopedActiveStream {
        RequestState* state;
        ~ScopedActiveStream() {
            if (state == nullptr)
                return;
            const juce::ScopedLock sl(state->streamLock);
            state->activeStream = nullptr;
        }
    };

    ScopedActiveStream activeStreamScope{req.state.get()};

    // Handed to the factory, which calls it as soon as the stream exists and before it blocks —
    // see StreamPublisher for why publishing has to happen that early.
    auto publish = [&req](juce::InputStream* published) {
        if (req.state == nullptr)
            return;

        {
            const juce::ScopedLock sl(req.state->streamLock);
            req.state->activeStream = published;
        }

        // A cancel() that arrived before this point found nothing to abort, so apply it now that
        // the stream is visible. Without this, such a request would run to completion.
        if (published != nullptr && req.state->cancelled.load())
            abortStream(*published);
    };

    stream = createStream(url.withPOSTData(jsonString),
                          juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                              .withConnectionTimeoutMs(120000)
                              .withStatusCode(&httpStatus)
                              .withResponseHeaders(&responseHeaders),
                          publish);

    // Checked before the HTTP-status branches below: an aborted connect or read looks exactly like
    // a network failure from here, and reporting a cancellation the user asked for as an error
    // they might try to debug would be actively misleading.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    // httpStatus is populated by the pointer above as soon as HTTP response headers are
    // received, independent of whether createStream() went on to hand back a stream — so
    // an explicit error status always takes priority over inspecting (or the absence of)
    // a body.
    if (httpStatus == 401 || httpStatus == 403) {
        deliverErr(AIErrorKind::Auth, "Error: Ollama at " + ollamaHost +
                                          " rejected the request as unauthorized (HTTP " + juce::String(httpStatus) +
                                          ").");
        return;
    }

    if (httpStatus == 429) {
        int retryAfterSeconds = responseHeaders.getValue("Retry-After", "").getIntValue();
        deliverErr(AIErrorKind::RateLimit, "Error: Ollama at " + ollamaHost + " rate-limited the request (HTTP 429).",
                   retryAfterSeconds);
        return;
    }

    if (httpStatus == 402) {
        deliverErr(AIErrorKind::Quota, "Error: Ollama at " + ollamaHost + " reported insufficient quota (HTTP 402).");
        return;
    }

    if (httpStatus != 0 && (httpStatus < 200 || httpStatus >= 300)) {
        deliverErr(AIErrorKind::Server,
                   "Error: Ollama at " + ollamaHost + " rejected the request (HTTP " + juce::String(httpStatus) + ").");
        return;
    }

    if (stream == nullptr) {
        deliverErr(AIErrorKind::Network, "Error: Could not connect to Ollama at " + ollamaHost);
        return;
    }

    juce::String responseText = stream->readEntireStreamAsString();
    // DBG("AI Chat Response (before JSON parse): [" + responseText + "]"); // Potentially expensive

    // A cancel that landed while we were reading: throw away whatever partial text came back.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    juce::var jsonResponse = juce::JSON::parse(responseText);
    if (jsonResponse.isObject()) {
        if (auto* obj = jsonResponse.getDynamicObject()) {
            if (obj->hasProperty("message")) {
                auto msgObj = obj->getProperty("message");
                if (msgObj.getDynamicObject()) {
                    deliverSuccess(msgObj.getDynamicObject()->getProperty("content").toString());
                    return;
                }
            }
        }
    }

    deliverErr(AIErrorKind::Schema, "Error: Ollama at " + ollamaHost + " returned an unparseable response.");
}

} // namespace synth