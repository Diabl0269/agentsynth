#include "OllamaProvider.h"

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
} // namespace

// Default constructor implementation
OllamaProvider::OllamaProvider(const juce::String& host)
    : Thread("OllamaProviderThread")
    , ollamaHost(host)
    , createStream([](const juce::URL& url, const juce::URL::InputStreamOptions& options) {
        return url.createInputStream(options);
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
    failAllPending(kShuttingDownMessage);

    // Join the discovery worker so it cannot touch our members after we are
    // destroyed (it captures `this`). With the short 4s connection timeout
    // and the in-flight guard ensuring at most ONE worker exists, this join
    // can block for at most ~4s in the worst case (server unreachable), never
    // the old 120s. We never reassign a joinable thread (the worker is only
    // launched when none is in flight), so std::terminate cannot fire here.
    if (discoveryThread.joinable())
        discoveryThread.join();
}

void OllamaProvider::sendPrompt(const std::vector<Message>& conversation, CompletionCallback callback,
                                const juce::var& responseSchema) {
    const Request request{conversation, std::move(callback), responseSchema};

    bool rejected = false;
    bool mustStartWorker = false;

    {
        const juce::ScopedLock sl(queueLock);

        if (isShuttingDown) {
            rejected = true;
        } else {
            pendingRequests.push_back(request);

            // Self-heal: `running` plus a dead thread means run() entered but ended
            // without releasing ownership - juce::Thread force-kills the worker when
            // stopThread() is given a zero timeout, and a throwing run() ends the same
            // way. Nobody is left to drain the queue, so take it over instead of waiting
            // forever. There is no false positive here: a worker that returns normally
            // sets `idle` before juce clears its handle.
            const bool ownerVanished = (workerState == WorkerState::running && !isThreadRunning());
            mustStartWorker = (workerState == WorkerState::idle) || ownerVanished;

            // Claim ownership now, under the same lock as the push, so exactly one
            // caller per retirement takes on starting the worker.
            if (mustStartWorker)
                workerState = WorkerState::starting;
        }
    }

    if (rejected) {
        deliverResult(request, kShuttingDownMessage, false, /*forceSynchronous=*/true);
        return;
    }

    if (mustStartWorker && !ensureWorkerRunning()) {
        // No worker could take the queue. Failing loudly beats the UI waiting forever.
        failAllPending("Error: Could not start the Ollama request worker thread.");
        return;
    }

    // Wake a worker parked in run()'s wait(). Harmless if it is busy or just started:
    // the event is auto-reset and every loop iteration re-checks the queue under lock,
    // so a stale signal only costs one extra iteration and a wakeup is never lost.
    notify();
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

void OllamaProvider::failAllPending(const juce::String& message) {
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
        deliverResult(req, message, false, deliverSynchronously);
}

void OllamaProvider::deliverResult(const Request& req, const juce::String& responseText, bool success,
                                   bool forceSynchronous) {
    if (!req.callback)
        return;

    if (isTestMode || forceSynchronous) {
        req.callback(responseText, success);
        return;
    }

    // Copy just the callback (not the whole conversation) and never `this`, so a
    // message already in flight cannot touch a destroyed provider.
    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, responseText, success]() { callback(responseText, success); });
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

        if (auto stream = createStream(
                url,
                juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress).withConnectionTimeoutMs(4000))) {
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
    failAllPending(kShuttingDownMessage);
}

void OllamaProvider::processRequest(const Request& req) {
    // Delivers (responseText, success) through the same test-mode-aware channel used by
    // every exit path below, so the fail-fast path and the network paths stay identical.
    auto deliver = [this, &req](const juce::String& responseText, bool success) {
        deliverResult(req, responseText, success, /*forceSynchronous=*/false);
    };

    // Fail fast: never hit the network with an empty model. Ollama rejects such requests
    // with HTTP 400 "model is required", which previously surfaced as a misleading
    // "Could not connect to Ollama" error even when the server was reachable.
    if (currentModel.isEmpty()) {
        deliver("Error: No Ollama model selected. Check that Ollama is running and that a model is available "
                "(ollama list).",
                false);
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

    juce::String responseText;
    bool success = false;
    int httpStatus = 0;

    if (auto stream = createStream(url.withPOSTData(jsonString),
                                   juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inPostData)
                                       .withConnectionTimeoutMs(120000)
                                       .withStatusCode(&httpStatus))) {
        responseText = stream->readEntireStreamAsString();
        // DBG("AI Chat Response (before JSON parse): [" + responseText + "]"); // Potentially expensive

        juce::var jsonResponse = juce::JSON::parse(responseText);
        if (jsonResponse.isObject()) {
            if (auto* obj = jsonResponse.getDynamicObject()) {
                if (obj->hasProperty("message")) {
                    auto msgObj = obj->getProperty("message");
                    if (msgObj.getDynamicObject()) {
                        responseText = msgObj.getDynamicObject()->getProperty("content").toString();
                        success = true;
                    }
                }
            }
        }
    } else if (httpStatus != 0) {
        responseText =
            "Error: Ollama at " + ollamaHost + " rejected the request (HTTP " + juce::String(httpStatus) + ").";
    } else {
        responseText = "Error: Could not connect to Ollama at " + ollamaHost;
    }

    deliver(responseText, success);
}

} // namespace synth