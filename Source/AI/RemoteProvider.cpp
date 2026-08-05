#include "RemoteProvider.h"
#include "../Branding.h"
#include <algorithm>

#ifndef _WIN32
#include <curl/curl.h>
#endif

namespace synth {

namespace {
// How long run() parks when the queue is empty. See OllamaProvider's kIdleWaitMs for the
// identical rationale: purely a lost-wakeup safety net, never a poll relied on.
constexpr int kIdleWaitMs = 1000;

// Upper bound on waiting for a retired worker's thread handle to be released so a replacement
// can start. See OllamaProvider's kWorkerHandoverTimeoutMs.
constexpr int kWorkerHandoverTimeoutMs = 2000;

// Same class of model, same generation-time ceiling as OllamaProvider's kChatRequestTimeoutMs
// (OllamaProvider.cpp): the service's patch.generate call is non-streaming, so this is the
// effective bound on how long a model has to produce a full response, not just a connect
// timeout. Kept identical rather than re-deriving a separate number for a request that is,
// from the client's point of view, the same shape of wait.
constexpr int kRequestTimeoutMs = 240000;

const char* const kShuttingDownMessage = "Error: Request cancelled - the Remote provider is shutting down.";
const char* const kCancelledMessage = "Request cancelled.";

/** Extracts `error.message` from a JSON error body, if present and a string. Returns an empty
    string otherwise — callers fall back to a generic message naming the HTTP status. */
juce::String extractErrorDetail(const juce::String& body) {
    auto parsed = juce::JSON::parse(body);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("error")) {
            auto errorVar = obj->getProperty("error");
            if (auto* errorObj = errorVar.getDynamicObject()) {
                auto message = errorObj->getProperty("message");
                if (message.isString())
                    return message.toString();
            }
        }
    }
    return {};
}

/** Appends ": <detail>" to `base` when the response body carries a parseable error.message,
    mirroring this codebase's "name the specific failure" ethos (see
    AIIntegrationService::buildCorrectionPrompt). Falls back to `base` alone otherwise. */
juce::String withErrorDetail(const juce::String& base, const juce::String& body) {
    const auto detail = extractErrorDetail(body);
    return detail.isNotEmpty() ? (base + ": " + detail) : base;
}

#ifndef _WIN32
size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<juce::String*>(userdata);
    *out += juce::String::fromUTF8(ptr, static_cast<int>(size * nmemb));
    return size * nmemb;
}

size_t curlHeaderCallback(char* buffer, size_t size, size_t nitems, void* userdata) {
    auto* headers = static_cast<juce::StringPairArray*>(userdata);
    const auto lineLength = size * nitems;
    juce::String line = juce::String::fromUTF8(buffer, static_cast<int>(lineLength)).trim();

    const int colon = line.indexOfChar(':');
    if (colon > 0) {
        const auto key = line.substring(0, colon).trim();
        const auto value = line.substring(colon + 1).trim();
        if (key.isNotEmpty())
            headers->set(key, value);
    }

    return lineLength;
}

int curlProgressCallback(void* clientp, curl_off_t, curl_off_t, curl_off_t, curl_off_t) {
    auto* cancelled = static_cast<const std::atomic<bool>*>(clientp);
    return (cancelled != nullptr && cancelled->load()) ? 1 : 0; // non-zero aborts the transfer
}

/** Real libcurl-backed HttpPerformer. Never touches a CURL* from any thread other than the one
    running curl_easy_perform() for it — this function IS that thread (whichever calls it). */
RemoteProvider::HttpResult performHttpWithCurl(const juce::String& url, const juce::StringPairArray& requestHeaders,
                                               const juce::String& jsonBody, int timeoutMs,
                                               const std::atomic<bool>& cancelled) {
    // curl_global_init() exactly once, process-wide — never per-request. An immediately-invoked
    // lambda initializing a function-local static is the simplest thread-safe way to do that
    // pre-C++11-magic-statics-notwithstanding (C++11 guarantees this init is itself thread-safe).
    static const bool globalInitDone = [] {
        curl_global_init(CURL_GLOBAL_DEFAULT);
        return true;
    }();
    juce::ignoreUnused(globalInitDone);

    RemoteProvider::HttpResult result;

    CURL* curl = curl_easy_init();
    if (curl == nullptr) {
        result.transportFailed = true;
        result.errorMessage = "Error: Could not initialize libcurl.";
        return result;
    }

    juce::String responseBody;
    juce::StringPairArray responseHeaders;

    curl_slist* headerList = nullptr;
    for (const auto& key : requestHeaders.getAllKeys()) {
        const auto headerLine = key + ": " + requestHeaders[key];
        headerList = curl_slist_append(headerList, headerLine.toRawUTF8());
    }

    curl_easy_setopt(curl, CURLOPT_URL, url.toRawUTF8());
    curl_easy_setopt(curl, CURLOPT_POST, 1L);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonBody.toRawUTF8());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(jsonBody.getNumBytesAsUTF8()));
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headerList);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curlWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &responseBody);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, curlHeaderCallback);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &responseHeaders);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(timeoutMs));
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(curl, CURLOPT_XFERINFOFUNCTION, curlProgressCallback);
    curl_easy_setopt(curl, CURLOPT_XFERINFODATA, &cancelled);

    const CURLcode res = curl_easy_perform(curl);

    long httpStatus = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpStatus);
    result.httpStatus = static_cast<int>(httpStatus);
    result.body = responseBody;
    result.headers = responseHeaders;

    if (res == CURLE_ABORTED_BY_CALLBACK) {
        // Treated as cancellation by the caller (which already knows it cancelled and checks
        // state->cancelled before looking at this result), so the message here is never surfaced.
        result.transportFailed = true;
        result.errorMessage = "Request aborted (cancelled).";
    } else if (res == CURLE_OPERATION_TIMEDOUT) {
        result.timedOut = true;
        result.errorMessage = "Error: Request to " + url + " timed out.";
    } else if (res != CURLE_OK) {
        result.transportFailed = true;
        result.errorMessage = juce::String("Error: ") + curl_easy_strerror(res);
    }

    if (headerList != nullptr)
        curl_slist_free_all(headerList);
    curl_easy_cleanup(curl);

    return result;
}
#else
RemoteProvider::HttpResult performHttpUnavailable(const juce::String&, const juce::StringPairArray&,
                                                  const juce::String&, int, const std::atomic<bool>&) {
    RemoteProvider::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = "Error: RemoteProvider is not available on Windows yet (no libcurl backend).";
    return result;
}
#endif

} // namespace

RemoteProvider::RemoteProvider(const juce::String& host)
    : Thread("RemoteProviderThread")
    , remoteHost(host)
#ifndef _WIN32
    , performHttp(performHttpWithCurl)
#else
    , performHttp(performHttpUnavailable)
#endif
{
}

RemoteProvider::RemoteProvider(const juce::String& host, HttpPerformer performer)
    : Thread("RemoteProviderThread")
    , remoteHost(host)
    , performHttp(std::move(performer)) {}

RemoteProvider::~RemoteProvider() {
    {
        const juce::ScopedLock sl(queueLock);
        isShuttingDown = true;
    }

    // Wakes run() (stopThread signals the exit flag and notify()s the thread's event), then
    // blocks until run() has returned. run() retires through failAllPending(), which delivers
    // the shutdown error for every still-queued request INLINE — see failAllPending() for why
    // that delivery must not be deferred here.
    stopThread(2000);

    // Belt and braces: covers the case where no worker ever ran (so nothing retired) and
    // anything that slipped into the queue during teardown.
    failAllPending(AIErrorKind::Cancelled, kShuttingDownMessage);
}

void RemoteProvider::setAuthToken(const juce::String& token) { authToken = token; }

RemoteProvider::RequestId RemoteProvider::sendPrompt(const std::vector<Message>& conversation,
                                                     CompletionCallback callback, const juce::var& responseSchema,
                                                     std::function<void(const juce::String&)> onDelta) {
    // Streaming is not implemented: onDelta is accepted for interface compatibility but
    // intentionally never invoked.
    juce::ignoreUnused(onDelta);

    Request request{RequestId{}, conversation, std::move(callback), responseSchema, nullptr};
    bool rejected = false;
    bool mustStartWorker = false;
    AIErrorKind rejectionKind = AIErrorKind::Cancelled;
    juce::String rejectionMessage = kShuttingDownMessage;

    {
        const juce::ScopedLock sl(queueLock);

        request.id.value = nextRequestId++;
        request.state = std::make_shared<RequestState>(request.id);

        if (isShuttingDown) {
            rejected = true;
        } else if (responseSchema.isVoid()) {
            // The remote service currently exposes only patch.generate (structured output); there
            // is no plain-chat capability to call. Fail fast, no network hit — mirrors
            // OllamaProvider's "no model selected" precedent.
            rejected = true;
            rejectionKind = AIErrorKind::Schema;
            rejectionMessage =
                "RemoteProvider only supports structured patch generation; the service has no conversational "
                "capability yet.";
        } else if (conversation.empty() || conversation.back().content.trim().isEmpty()) {
            rejected = true;
            rejectionKind = AIErrorKind::Schema;
            rejectionMessage = "Error: Cannot send an empty prompt to the Remote provider.";
        } else {
            // Registered under the same lock as the queue push, so a cancel() racing this call
            // either does not see the request at all or sees it in both places.
            inFlight.emplace(request.id.value, request.state);
            pendingRequests.push_back(request);

            const bool ownerVanished = (workerState == WorkerState::running && !isThreadRunning());
            mustStartWorker = (workerState == WorkerState::idle) || ownerVanished;

            if (mustStartWorker)
                workerState = WorkerState::starting;
        }
    }

    const RequestId requestId = request.id;

    if (rejected) {
        deliverError(request, rejectionKind, rejectionMessage, /*retryAfterSeconds=*/0, /*forceSynchronous=*/true);
        return requestId;
    }

    if (mustStartWorker && !ensureWorkerRunning()) {
        failAllPending(AIErrorKind::Server, "Error: Could not start the Remote provider request worker thread.");
        return requestId;
    }

    notify();

    return requestId;
}

void RemoteProvider::cancel(RequestId requestId) {
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

        const auto queued = std::find_if(pendingRequests.begin(), pendingRequests.end(),
                                         [&state](const Request& r) { return r.state == state; });

        if (queued != pendingRequests.end()) {
            abandoned = *queued;
            pendingRequests.erase(queued);
            tookFromQueue = true;
        }
    }

    if (tookFromQueue) {
        deliverError(abandoned, AIErrorKind::Cancelled, kCancelledMessage, /*retryAfterSeconds=*/0,
                     /*forceSynchronous=*/false);
        return;
    }

    // Already in flight: nothing more to do here. The cancelled flag is polled by the HTTP
    // performer's own progress callback during curl_easy_perform(), on the worker thread, which
    // notices it and unwinds on its own; the worker delivers the Cancelled callback itself.
}

bool RemoteProvider::claimDelivery(const Request& req) {
    if (req.state == nullptr)
        return true;

    if (req.state->delivered.exchange(true))
        return false;

    const juce::ScopedLock sl(queueLock);
    inFlight.erase(req.state->id.value);
    return true;
}

bool RemoteProvider::ensureWorkerRunning() {
    if (startThread())
        return true;

    if (juce::Thread::getCurrentThreadId() == getThreadId())
        return false;

    if (!waitForThreadToExit(kWorkerHandoverTimeoutMs))
        return false;

    return startThread();
}

void RemoteProvider::failAllPending(AIErrorKind kind, const juce::String& message) {
    std::vector<Request> stranded;
    bool deliverSynchronously = false;

    {
        const juce::ScopedLock sl(queueLock);
        stranded.swap(pendingRequests);
        workerState = WorkerState::idle;
        deliverSynchronously = isShuttingDown;
    }

    for (const auto& req : stranded)
        deliverError(req, kind, message, /*retryAfterSeconds=*/0, deliverSynchronously);
}

void RemoteProvider::deliverResult(const Request& req, const juce::String& responseText, bool forceSynchronous) {
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

    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, response]() { callback(response); });
}

void RemoteProvider::deliverError(const Request& req, AIErrorKind kind, const juce::String& message,
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

    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, response]() { callback(response); });
}

void RemoteProvider::setModel(const juce::String& name) {
    // Cosmetic only: patch.generate's input schema has no model field, and there is no
    // models-list endpoint (only GET /health and POST /v1/capability/:name exist) — the service
    // picks its own model server-side. Stored so getCurrentModel() round-trips, never sent.
    currentModel = name;
}

juce::String RemoteProvider::getCurrentModel() const { return currentModel; }

void RemoteProvider::fetchAvailableModels(std::function<void(const juce::StringArray& models, bool success)> callback) {
    // Nothing to enumerate — see setModel()'s comment — so this is a synchronous "success, empty
    // list" rather than a failure. Delivered the same test-mode/async-aware way as every other
    // callback in this class.
    if (!callback)
        return;

    if (isTestMode) {
        callback(juce::StringArray{}, true);
    } else {
        juce::MessageManager::callAsync([callback]() { callback(juce::StringArray{}, true); });
    }
}

void RemoteProvider::run() {
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

    failAllPending(AIErrorKind::Cancelled, kShuttingDownMessage);
}

void RemoteProvider::processRequest(const Request& req) {
    auto deliverSuccess = [this, &req](const juce::String& responseText) {
        deliverResult(req, responseText, /*forceSynchronous=*/false);
    };
    auto deliverErr = [this, &req](AIErrorKind kind, const juce::String& message, int retryAfterSeconds = 0) {
        deliverError(req, kind, message, retryAfterSeconds, /*forceSynchronous=*/false);
    };

    auto wasCancelled = [&req] { return req.state != nullptr && req.state->cancelled.load(); };

    // Cancelled while it sat in the queue, or during the handoff to this thread. Bail before
    // opening a connection.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    // Build the request body: {"productName": ..., "userPrompt": ...}. currentPatch and
    // promptVersion are deliberately OMITTED entirely (not sent as null).
    //
    // Why this is safe: AIIntegrationService::buildPatchAugmentedContent() already renders the
    // current graph state into the LAST conversation message as
    // "Current patch state:\n```json\n<JSON>\n```\n\nUser request: <text>" whenever the graph is
    // non-empty, or leaves it as plain text otherwise. The service's own buildUserMessage()
    // (synth-platform/packages/capabilities/src/patch-generate/capability.ts) performs the EXACT
    // SAME wrapping server-side, from a separate currentPatch + userPrompt pair, and only wraps
    // when currentPatch is present. Sending the client's already-wrapped text as userPrompt alone,
    // with currentPatch omitted, therefore produces byte-identical model input to the "proper"
    // structured split — without any fragile re-parsing of the wrapper on this side. Do not
    // "fix" this into a parser that splits userPrompt back into currentPatch/userPrompt.
    const juce::String userPrompt = req.conversation.back().content;

    juce::DynamicObject::Ptr body = new juce::DynamicObject();
    body->setProperty("productName", juce::String(synth::branding::kProductName));
    body->setProperty("userPrompt", userPrompt);

    const juce::String jsonBody = juce::JSON::toString(juce::var(body.get()));

    juce::StringPairArray requestHeaders;
    requestHeaders.set("Content-Type", "application/json");
    if (authToken.isNotEmpty())
        requestHeaders.set("Authorization", "Bearer " + authToken);

    const juce::String url = remoteHost + "/v1/capability/patch.generate";

    // req.state is only ever null for a default-constructed Request, which is never what the
    // worker pulls off pendingRequests (every enqueued Request gets a freshly made RequestState).
    jassert(req.state != nullptr);
    const HttpResult result = performHttp(url, requestHeaders, jsonBody, kRequestTimeoutMs, req.state->cancelled);

    // Checked BEFORE inspecting the HTTP result — same as OllamaProvider checks wasCancelled()
    // right after the network call returns: an aborted transfer looks exactly like a network
    // failure from here, and reporting a cancellation the user asked for as an error they might
    // try to debug would be actively misleading.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    if (result.transportFailed) {
        deliverErr(AIErrorKind::Network,
                   withErrorDetail("Error: Could not reach the Remote provider at " + remoteHost, result.body));
        return;
    }

    if (result.timedOut) {
        deliverErr(AIErrorKind::Timeout, "Error: Request to " + remoteHost + " timed out.");
        return;
    }

    const int httpStatus = result.httpStatus;

    if (httpStatus == 401 || httpStatus == 403) {
        deliverErr(AIErrorKind::Auth, withErrorDetail("Error: Remote provider at " + remoteHost +
                                                          " rejected the request as "
                                                          "unauthorized (HTTP " +
                                                          juce::String(httpStatus) + ")",
                                                      result.body));
        return;
    }

    if (httpStatus == 429) {
        const int retryAfterSeconds = result.headers.getValue("Retry-After", "").getIntValue();
        deliverErr(AIErrorKind::RateLimit,
                   withErrorDetail("Error: Remote provider at " + remoteHost + " rate-limited the request (HTTP 429)",
                                   result.body),
                   retryAfterSeconds);
        return;
    }

    if (httpStatus == 402) {
        deliverErr(AIErrorKind::Quota, withErrorDetail("Error: Remote provider at " + remoteHost +
                                                           " reported insufficient quota (HTTP 402)",
                                                       result.body));
        return;
    }

    if (httpStatus == 400 || httpStatus == 404) {
        // Client/request-shape problem — bad JSON, or the client and service disagree about what
        // capability exists. Not worth retrying with the same input.
        deliverErr(AIErrorKind::Schema,
                   withErrorDetail("Error: Remote provider at " + remoteHost + " rejected the request (HTTP " +
                                       juce::String(httpStatus) + ")",
                                   result.body));
        return;
    }

    if (httpStatus < 200 || httpStatus >= 300) {
        // 500, 502, or any other unexpected non-2xx.
        deliverErr(AIErrorKind::Server,
                   withErrorDetail("Error: Remote provider at " + remoteHost + " failed the request (HTTP " +
                                       juce::String(httpStatus) + ")",
                                   result.body));
        return;
    }

    juce::var jsonResponse = juce::JSON::parse(result.body);
    if (auto* obj = jsonResponse.getDynamicObject()) {
        if (obj->hasProperty("data")) {
            deliverSuccess(juce::JSON::toString(obj->getProperty("data")));
            return;
        }
    }

    deliverErr(AIErrorKind::Schema,
               "Error: Remote provider at " + remoteHost + " returned a 2xx response with no usable \"data\".");
}

} // namespace synth
