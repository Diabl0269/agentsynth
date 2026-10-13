#include "RemoteProvider.h"
#include "../Auth/DeviceIdStore.h"
#include "../Branding.h"
#include "SongSteps/SongSteps.h"
#include <algorithm>

#ifdef _WIN32
#include "WinHttpTransport.h"
#else
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

// Same class of model, same generation-time ceiling as OllamaProvider's
// kDefaultChatRequestTimeoutMs (OllamaProvider.cpp): the service's patch.generate call is
// non-streaming, so this is the effective bound on how long a model has to produce a full
// response, not just a connect timeout. Kept identical rather than re-deriving a separate number
// for a request that is, from the client's point of view, the same shape of wait. This is now
// just the default — the instance member requestTimeoutMs (RemoteProvider.h) is what's actually
// used, and is user-configurable via AIIntegrationService::setRequestTimeoutMs().
constexpr int kDefaultRequestTimeoutMs = 240000;

// A streamed build can run for minutes (the server allows 10), kept alive by its pings, so it gets its own ceiling
// rather than the one-answer timeout.
constexpr int kStreamTimeoutMs = 660000;

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

/** Extracts `error.code` from a JSON error body (e.g. "TRIAL_EXHAUSTED",
    "SERVICE_CAPACITY_EXCEEDED"), if present and a string. Returns an empty string otherwise —
    callers fall back to the generic HTTP-status-only mapping for that status. */
juce::String extractErrorCode(const juce::String& body) {
    auto parsed = juce::JSON::parse(body);
    if (auto* obj = parsed.getDynamicObject()) {
        if (obj->hasProperty("error")) {
            auto errorVar = obj->getProperty("error");
            if (auto* errorObj = errorVar.getDynamicObject()) {
                auto code = errorObj->getProperty("code");
                if (code.isString())
                    return code.toString();
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
// Where a transfer's body goes: a 2xx body to `onBody` when the caller streams, everything else (and every body of
// a plain request) appended to `text`.
struct CurlBodySink {
    CURL* curl = nullptr;
    juce::String text;
    const std::function<void(const char*, std::size_t)>* onBody = nullptr;
};

size_t curlWriteCallback(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* sink = static_cast<CurlBodySink*>(userdata);
    if (sink->onBody != nullptr) {
        long status = 0;
        curl_easy_getinfo(sink->curl, CURLINFO_RESPONSE_CODE, &status);
        if (status >= 200 && status < 300) {
            (*sink->onBody)(ptr, size* nmemb);
            return size * nmemb;
        }
    }
    sink->text += juce::String::fromUTF8(ptr, static_cast<int>(size * nmemb));
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
                                               const std::atomic<bool>& cancelled,
                                               const std::function<void(const char*, std::size_t)>* onBody = nullptr) {
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

    CurlBodySink sink;
    sink.curl = curl;
    sink.onBody = onBody;
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
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &sink);
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
    result.body = sink.text;
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

RemoteProvider::HttpResult streamHttpWithCurl(const juce::String& url, const juce::StringPairArray& requestHeaders,
                                              const juce::String& jsonBody, int timeoutMs,
                                              const std::atomic<bool>& cancelled,
                                              const std::function<void(const char*, std::size_t)>& onBody) {
    return performHttpWithCurl(url, requestHeaders, jsonBody, timeoutMs, cancelled, &onBody);
}
#else
/** WinHTTP-backed HttpPerformer for Windows (see WinHttpTransport.h). */
RemoteProvider::HttpResult performHttpWithWinHttp(const juce::String& url, const juce::StringPairArray& requestHeaders,
                                                  const juce::String& jsonBody, int timeoutMs,
                                                  const std::atomic<bool>& cancelled,
                                                  const std::function<void(const char*, std::size_t)>& onBody = {}) {
    auto r = performWinHttpRequest("POST", url, requestHeaders, jsonBody, timeoutMs, cancelled, onBody);
    RemoteProvider::HttpResult result;
    result.httpStatus = r.httpStatus;
    result.body = std::move(r.body);
    result.headers = std::move(r.headers);
    result.transportFailed = r.transportFailed;
    result.timedOut = r.timedOut;
    result.errorMessage = std::move(r.errorMessage);
    return result;
}

RemoteProvider::HttpResult streamHttpWithWinHttp(const juce::String& url, const juce::StringPairArray& requestHeaders,
                                                 const juce::String& jsonBody, int timeoutMs,
                                                 const std::atomic<bool>& cancelled,
                                                 const std::function<void(const char*, std::size_t)>& onBody) {
    return performHttpWithWinHttp(url, requestHeaders, jsonBody, timeoutMs, cancelled, onBody);
}
#endif

} // namespace

RemoteProvider::RemoteProvider(const juce::String& host)
    : Thread("RemoteProviderThread")
    , remoteHost(host)
    // DeviceIdStore() (no explicit path) resolves to the app's standard settings-folder
    // convention and persists on first read — see Source/Auth/DeviceIdStore.h.
    , deviceId(DeviceIdStore().getDeviceId())
#ifndef _WIN32
    , performHttp([](const juce::String& u, const juce::StringPairArray& h, const juce::String& b, int t,
                     const std::atomic<bool>& c) { return performHttpWithCurl(u, h, b, t, c); })
    , performStream(streamHttpWithCurl)
#else
    , performHttp([](const juce::String& u, const juce::StringPairArray& h, const juce::String& b, int t,
                     const std::atomic<bool>& c) { return performHttpWithWinHttp(u, h, b, t, c); })
    , performStream(streamHttpWithWinHttp)
#endif
{
}

RemoteProvider::RemoteProvider(const juce::String& host, HttpPerformer performer, juce::String deviceIdIn,
                               StreamPerformer streamPerformer)
    : Thread("RemoteProviderThread")
    , remoteHost(host)
    , deviceId(std::move(deviceIdIn))
    , performHttp(std::move(performer))
    , performStream(std::move(streamPerformer)) {
    if (!performStream) {
        // No streaming transport injected: run the plain one and hand its whole body over as a single chunk.
        performStream = [this](const juce::String& url, const juce::StringPairArray& headers, const juce::String& body,
                               int timeoutMs, const std::atomic<bool>& cancelled,
                               const std::function<void(const char*, std::size_t)>& onBody) {
            auto result = performHttp(url, headers, body, timeoutMs, cancelled);
            if (result.httpStatus >= 200 && result.httpStatus < 300 && !result.transportFailed && !result.timedOut) {
                const auto bytes = result.body.toStdString();
                onBody(bytes.data(), bytes.size());
                result.body = {};
            }
            return result;
        };
    }
}

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

void RemoteProvider::setConversationId(const juce::String& id) { conversationId = id; }

RemoteProvider::RequestId RemoteProvider::sendPrompt(const std::vector<Message>& conversation,
                                                     CompletionCallback callback, const juce::var& responseSchema,
                                                     std::function<void(const juce::String&)> onDelta) {
    // Streaming is not implemented: onDelta is accepted for interface compatibility but
    // intentionally never invoked.
    juce::ignoreUnused(onDelta);

    Request request{RequestId{}, conversation, std::move(callback), responseSchema, nullptr};

    AIErrorKind validationKind = AIErrorKind::None;
    juce::String validationMessage;
    if (responseSchema.isVoid()) {
        // The remote service exposes no plain-chat capability to call — sendPrompt() is always a
        // structured patch.generate request. Fail fast, no network hit — mirrors OllamaProvider's
        // "no model selected" precedent. (Structured non-conversational requests go through
        // sendCapabilityRequest() instead.)
        validationKind = AIErrorKind::Schema;
        validationMessage =
            "RemoteProvider only supports structured patch generation; the service has no conversational "
            "capability yet.";
    } else if (conversation.empty() || conversation.back().content.trim().isEmpty()) {
        validationKind = AIErrorKind::Schema;
        validationMessage = "Error: Cannot send an empty prompt to the Remote provider.";
    }

    return enqueueOrReject(std::move(request), validationKind, validationMessage);
}

RemoteProvider::RequestId RemoteProvider::sendCapabilityRequest(const juce::String& capability, const juce::var& body,
                                                                CompletionCallback callback) {
    Request request{RequestId{}, {}, std::move(callback), juce::var(), nullptr};
    request.capability = capability;

    AIErrorKind validationKind = AIErrorKind::None;
    juce::String validationMessage;

    auto* bodyObj = body.getDynamicObject();
    if (capability.trim().isEmpty()) {
        validationKind = AIErrorKind::Schema;
        validationMessage = "Error: Cannot send a capability request with no capability name.";
    } else if (bodyObj == nullptr || !body["userPrompt"].isString() || body["userPrompt"].toString().trim().isEmpty()) {
        // Every capability's input schema requires a non-empty userPrompt — reject here, with no
        // network hit, the same way sendPrompt() rejects a blank last message.
        validationKind = AIErrorKind::Schema;
        validationMessage = "Error: A capability request body must be an object with a non-empty \"userPrompt\".";
    } else {
        // Final body = the caller's fields plus productName (the provider owns branding, callers
        // never restate it — same division as the patch path). Serialized HERE, on the calling
        // thread, into Request::capabilityBodyJson — see the header doc comment for why a string
        // crosses to the worker rather than a ref-counted juce::var. The caller's object is read,
        // never mutated.
        juce::DynamicObject::Ptr finalBody = new juce::DynamicObject();
        for (const auto& prop : bodyObj->getProperties())
            finalBody->setProperty(prop.name, prop.value);
        finalBody->setProperty("productName", juce::String(synth::branding::kProductName));
        request.capabilityBodyJson = juce::JSON::toString(juce::var(finalBody.get()));
    }

    return enqueueOrReject(std::move(request), validationKind, validationMessage);
}

juce::String RemoteProvider::routeBodyJson(const juce::var& body) {
    juce::DynamicObject::Ptr finalBody = new juce::DynamicObject();
    if (auto* object = body.getDynamicObject())
        for (const auto& prop : object->getProperties())
            finalBody->setProperty(prop.name, prop.value);
    finalBody->setProperty("productName", juce::String(synth::branding::kProductName));
    return juce::JSON::toString(juce::var(finalBody.get()));
}

RemoteProvider::RequestId RemoteProvider::enqueueRoute(const juce::String& route, const juce::var& body,
                                                       CompletionCallback callback, StreamLineCallback onLine) {
    Request request{RequestId{}, {}, std::move(callback), juce::var(), nullptr};
    request.route = route;
    request.onLine = std::move(onLine);

    AIErrorKind validationKind = AIErrorKind::None;
    juce::String validationMessage;
    if (!route.startsWith("/")) {
        validationKind = AIErrorKind::Schema;
        validationMessage = "Error: A service route starts with a slash.";
    } else if (body.getDynamicObject() == nullptr) {
        validationKind = AIErrorKind::Schema;
        validationMessage = "Error: A service request body must be a JSON object.";
    } else {
        request.capabilityBodyJson = routeBodyJson(body);
    }
    return enqueueOrReject(std::move(request), validationKind, validationMessage);
}

RemoteProvider::RequestId RemoteProvider::sendRouteRequest(const juce::String& route, const juce::var& body,
                                                           CompletionCallback callback) {
    return enqueueRoute(route, body, std::move(callback), {});
}

RemoteProvider::RequestId RemoteProvider::streamRouteRequest(const juce::String& route, const juce::var& body,
                                                             StreamLineCallback onLine, CompletionCallback onFinished) {
    if (!onLine)
        onLine = [](const juce::String&) {};
    return enqueueRoute(route, body, std::move(onFinished), std::move(onLine));
}

RemoteProvider::RequestId RemoteProvider::enqueueOrReject(Request&& request, AIErrorKind validationKind,
                                                          const juce::String& validationMessage) {
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
        } else if (validationMessage.isNotEmpty()) {
            rejected = true;
            rejectionKind = validationKind;
            rejectionMessage = validationMessage;
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

void RemoteProvider::deliverResult(const Request& req, const juce::String& responseText, bool forceSynchronous,
                                   const juce::String& conversationId, const juce::String& messageId) {
    if (!claimDelivery(req))
        return;

    if (!req.callback)
        return;

    AIResponse response;
    response.success = true;
    response.content = responseText;
    response.requestId = juce::String(req.id.value);
    response.conversationId = conversationId;
    response.messageId = messageId;

    if (isTestMode || forceSynchronous) {
        req.callback(response);
        return;
    }

    auto callback = req.callback;
    juce::MessageManager::callAsync([callback, response]() { callback(response); });
}

void RemoteProvider::deliverError(const Request& req, AIErrorKind kind, const juce::String& message,
                                  int retryAfterSeconds, bool forceSynchronous, int httpStatus,
                                  const juce::String& code) {
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
    response.error.httpStatus = httpStatus;
    response.error.code = code;

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

void RemoteProvider::setRequestTimeoutMs(int timeoutMs) { requestTimeoutMs = timeoutMs; }
int RemoteProvider::getRequestTimeoutMs() const { return requestTimeoutMs; }

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

// One status-to-error mapping serves every capability and route, which is what keeps entitlement errors (quota, trial,
// capacity) identical across them. TRIAL_EXHAUSTED and SERVICE_CAPACITY_EXCEEDED get their own kinds because the app
// words them differently from a generic quota or server failure; the server's own message is kept verbatim for them.
RemoteProvider::Failure RemoteProvider::classifyFailure(const HttpResult& result) const {
    Failure f;
    f.httpStatus = result.httpStatus;
    auto fail = [&](AIErrorKind kind, const juce::String& message, int retryAfterSeconds = 0) {
        f.kind = kind;
        f.message = message;
        f.retryAfterSeconds = retryAfterSeconds;
        f.code = extractErrorCode(result.body);
        return f;
    };

    if (result.transportFailed)
        return fail(AIErrorKind::Network,
                    withErrorDetail("Error: Could not reach the Remote provider at " + remoteHost, result.body));
    if (result.timedOut)
        return fail(AIErrorKind::Timeout, "Error: Request to " + remoteHost + " timed out.");

    const int httpStatus = result.httpStatus;
    const juce::String code = extractErrorCode(result.body);
    const juce::String detail = extractErrorDetail(result.body);
    const juce::String at = "Error: Remote provider at " + remoteHost;

    if (httpStatus == 401 || httpStatus == 403)
        return fail(AIErrorKind::Auth, withErrorDetail(at + " rejected the request as unauthorized (HTTP " +
                                                           juce::String(httpStatus) + ")",
                                                       result.body));
    if (httpStatus == 429) {
        // The monthly quota answers 429 QUOTA_EXCEEDED; any other 429 keeps the generic rate-limit mapping.
        if (code == "QUOTA_EXCEEDED")
            return fail(AIErrorKind::Quota,
                        detail.isNotEmpty() ? detail : "Error: Your monthly request quota is used up.");
        return fail(AIErrorKind::RateLimit, withErrorDetail(at + " rate-limited the request (HTTP 429)", result.body),
                    result.headers.getValue("Retry-After", "").getIntValue());
    }
    if (httpStatus == 402) {
        if (code == "TRIAL_EXHAUSTED")
            return fail(AIErrorKind::TrialExhausted,
                        detail.isNotEmpty() ? detail : "Error: Your free trial has been used up.");
        return fail(AIErrorKind::Quota, withErrorDetail(at + " reported insufficient quota (HTTP 402)", result.body));
    }
    if (httpStatus == 503 && code == "SERVICE_CAPACITY_EXCEEDED")
        return fail(AIErrorKind::ServiceCapacityExceeded,
                    detail.isNotEmpty() ? detail
                                        : "Error: The service is at capacity right now. Please try again shortly.");
    if (httpStatus == 400 || httpStatus == 404)
        // A request-shape problem (or a client and service that disagree about what exists): not worth retrying as is.
        return fail(AIErrorKind::Schema,
                    withErrorDetail(at + " rejected the request (HTTP " + juce::String(httpStatus) + ")", result.body));
    if (httpStatus < 200 || httpStatus >= 300)
        return fail(AIErrorKind::Server,
                    withErrorDetail(at + " failed the request (HTTP " + juce::String(httpStatus) + ")", result.body));
    return f;
}

// A streamed request holds the worker for the whole build, which is fine: nothing else the user can start waits on
// it. Lines are cut from the byte stream on this thread and posted to the message thread in order; a cancel() drops
// the lines still queued (the posted lambda checks the flag) and closes the connection through the progress callback.
void RemoteProvider::processStream(const Request& req, const juce::String& url, const juce::StringPairArray& headers,
                                   const juce::String& jsonBody) {
    const bool synchronous = isTestMode;
    auto state = req.state;
    auto onLine = req.onLine;
    auto post = [state, onLine, synchronous](const juce::String& line) {
        if (synchronous) {
            if (!state->cancelled.load())
                onLine(line);
            return;
        }
        juce::MessageManager::callAsync([state, onLine, line] {
            if (!state->cancelled.load())
                onLine(line);
        });
    };

    synth::songsteps::LineSplitter splitter;
    bool streamed = false;
    const auto onBody = [&](const char* data, std::size_t size) {
        streamed = true;
        splitter.push(data, size, post);
    };
    const HttpResult result = performStream(url, headers, jsonBody, kStreamTimeoutMs, state->cancelled, onBody);

    if (state->cancelled.load()) {
        deliverError(req, AIErrorKind::Cancelled, kCancelledMessage, 0, /*forceSynchronous=*/false);
        return;
    }
    if (const auto failure = classifyFailure(result); failure.kind != AIErrorKind::None) {
        deliverError(req, failure.kind, failure.message, failure.retryAfterSeconds, /*forceSynchronous=*/false,
                     failure.httpStatus, failure.code);
        return;
    }
    splitter.finish(post);
    juce::ignoreUnused(streamed);
    deliverResult(req, {}, /*forceSynchronous=*/false);
}

void RemoteProvider::processRequest(const Request& req) {
    auto deliverSuccess = [this, &req](const juce::String& responseText, const juce::String& respConversationId,
                                       const juce::String& respMessageId) {
        deliverResult(req, responseText, /*forceSynchronous=*/false, respConversationId, respMessageId);
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

    // A capability request (sendCapabilityRequest) arrives with its endpoint name and its final
    // body already decided — the enqueuing thread serialized it, this thread only sends it.
    const bool isCapabilityRequest = req.capability.isNotEmpty();
    const bool isRouteRequest = req.route.isNotEmpty();

    juce::String jsonBody;
    if (isCapabilityRequest || isRouteRequest) {
        jsonBody = req.capabilityBodyJson;
    } else {
        // Legacy sendPrompt() path: build the patch.generate body {"productName": ...,
        // "userPrompt": ...}. currentPatch and promptVersion are deliberately OMITTED entirely
        // (not sent as null).
        //
        // Why this is safe: AIIntegrationService::buildPatchAugmentedContent() already renders the
        // current graph state into the LAST conversation message as
        // "Current patch state:\n```json\n<JSON>\n```\n\nUser request: <text>" whenever the graph
        // is non-empty, or leaves it as plain text otherwise. The service's own buildUserMessage()
        // (synth-platform/packages/capabilities/src/patch-generate/capability.ts) performs the
        // EXACT SAME wrapping server-side, from a separate currentPatch + userPrompt pair, and
        // only wraps when currentPatch is present. Sending the client's already-wrapped text as
        // userPrompt alone, with currentPatch omitted, therefore produces byte-identical model
        // input to the "proper" structured split — without any fragile re-parsing of the wrapper
        // on this side. Do not "fix" this into a parser that splits userPrompt back into
        // currentPatch/userPrompt.
        //
        // timeline.generate is DIFFERENT on exactly this point: its server-side
        // buildTimelineUserMessage() composes the context sections (arrangement, tracks, targets)
        // from the structured fields unconditionally, so its userPrompt must stay the RAW user
        // text — pre-wrapping it patch-style would put every section in the model input twice.
        // That is why arrange mode goes through sendCapabilityRequest() with structured fields
        // rather than through this path.
        const juce::String userPrompt = req.conversation.back().content;

        juce::DynamicObject::Ptr body = new juce::DynamicObject();
        body->setProperty("productName", juce::String(synth::branding::kProductName));
        body->setProperty("userPrompt", userPrompt);

        jsonBody = juce::JSON::toString(juce::var(body.get()));
    }

    juce::StringPairArray requestHeaders;
    requestHeaders.set("Content-Type", "application/json");
    if (authToken.isNotEmpty())
        requestHeaders.set("Authorization", "Bearer " + authToken);
    // Sent whether or not authToken is set: an anonymous free-request-tier signal when there's no
    // bearer token, anti-abuse signal when there is one. Harmless either way (not a secret).
    if (deviceId.isNotEmpty())
        requestHeaders.set("X-Device-Id", deviceId);
    // Sent only when AIIntegrationService/AIChatComponent believe this session has a persisted
    // server-side conversation to continue (Pro plan, and a prior response returned an id) — see
    // AIProvider::setConversationId()'s doc comment. Never synthesized here.
    if (conversationId.isNotEmpty())
        requestHeaders.set("x-conversation-id", conversationId);

    // One URL scheme for every capability; one status→error mapping below serves them all, which
    // is what keeps entitlement errors (quota/trial/capacity) byte-identical across capabilities.
    const juce::String url =
        isRouteRequest
            ? remoteHost + req.route
            : remoteHost + "/v1/capability/" + (isCapabilityRequest ? req.capability : juce::String("patch.generate"));

    if (req.onLine) {
        processStream(req, url, requestHeaders, jsonBody);
        return;
    }

    // req.state is only ever null for a default-constructed Request, which is never what the
    // worker pulls off pendingRequests (every enqueued Request gets a freshly made RequestState).
    jassert(req.state != nullptr);
    const HttpResult result = performHttp(url, requestHeaders, jsonBody, requestTimeoutMs, req.state->cancelled);

    // Checked BEFORE inspecting the HTTP result — same as OllamaProvider checks wasCancelled()
    // right after the network call returns: an aborted transfer looks exactly like a network
    // failure from here, and reporting a cancellation the user asked for as an error they might
    // try to debug would be actively misleading.
    if (wasCancelled()) {
        deliverErr(AIErrorKind::Cancelled, kCancelledMessage);
        return;
    }

    if (const auto failure = classifyFailure(result); failure.kind != AIErrorKind::None) {
        deliverError(req, failure.kind, failure.message, failure.retryAfterSeconds, /*forceSynchronous=*/false,
                     failure.httpStatus, failure.code);
        return;
    }

    juce::var jsonResponse = juce::JSON::parse(result.body);
    if (auto* obj = jsonResponse.getDynamicObject()) {
        if (obj->hasProperty("data")) {
            deliverSuccess(juce::JSON::toString(obj->getProperty("data")),
                           result.headers.getValue("x-conversation-id", ""),
                           result.headers.getValue("x-message-id", ""));
            return;
        }
    }

    deliverErr(AIErrorKind::Schema,
               "Error: Remote provider at " + remoteHost + " returned a 2xx response with no usable \"data\".");
}

} // namespace synth
