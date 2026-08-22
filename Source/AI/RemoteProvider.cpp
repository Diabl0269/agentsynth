#include "RemoteProvider.h"
#include "../Auth/DeviceIdStore.h"
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

// Same class of model, same generation-time ceiling as OllamaProvider's
// kDefaultChatRequestTimeoutMs (OllamaProvider.cpp): the service's patch.generate call is
// non-streaming, so this is the effective bound on how long a model has to produce a full
// response, not just a connect timeout. Kept identical rather than re-deriving a separate number
// for a request that is, from the client's point of view, the same shape of wait. This is now
// just the default — the instance member requestTimeoutMs (RemoteProvider.h) is what's actually
// used, and is user-configurable via AIIntegrationService::setRequestTimeoutMs().
constexpr int kDefaultRequestTimeoutMs = 240000;

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
    // DeviceIdStore() (no explicit path) resolves to the app's standard settings-folder
    // convention and persists on first read — see Source/Auth/DeviceIdStore.h.
    , deviceId(DeviceIdStore().getDeviceId())
#ifndef _WIN32
    , performHttp(performHttpWithCurl)
#else
    , performHttp(performHttpUnavailable)
#endif
{
}

RemoteProvider::RemoteProvider(const juce::String& host, HttpPerformer performer, juce::String deviceIdIn)
    : Thread("RemoteProviderThread")
    , remoteHost(host)
    , deviceId(std::move(deviceIdIn))
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

    juce::String jsonBody;
    if (isCapabilityRequest) {
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
        remoteHost + "/v1/capability/" + (isCapabilityRequest ? req.capability : juce::String("patch.generate"));

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
        // P4-3's monthly quota (enforce-quota.ts) answers 429 QUOTA_EXCEEDED, not the 402 the
        // Quota mapping below was originally written for — without this check, the real
        // paid-plan quota-exceeded response is silently misclassified as a generic rate limit and
        // the upgrade-bubble UI (P4-4) never fires. Any other/no code on a 429 keeps the
        // pre-existing generic RateLimit mapping — nothing else currently returns 429 to
        // /v1/capability/*, so this is defensive.
        if (extractErrorCode(result.body) == "QUOTA_EXCEEDED") {
            const auto detail = extractErrorDetail(result.body);
            deliverErr(AIErrorKind::Quota,
                       detail.isNotEmpty() ? detail : "Error: Your monthly request quota is used up.");
            return;
        }

        const int retryAfterSeconds = result.headers.getValue("Retry-After", "").getIntValue();
        deliverErr(AIErrorKind::RateLimit,
                   withErrorDetail("Error: Remote provider at " + remoteHost + " rate-limited the request (HTTP 429)",
                                   result.body),
                   retryAfterSeconds);
        return;
    }

    if (httpStatus == 402) {
        // TRIAL_EXHAUSTED is a distinct, user-facing state (the free-trial-to-account conversion
        // moment — see AIErrorKind::TrialExhausted's doc comment), not a generic quota failure: the
        // server's message is surfaced verbatim rather than folded into a canned "insufficient
        // quota" string, since (when the caller isn't signed in) that message specifically invites
        // signing in to continue. Any other 402 shape keeps the pre-existing generic Quota mapping.
        if (extractErrorCode(result.body) == "TRIAL_EXHAUSTED") {
            const auto detail = extractErrorDetail(result.body);
            deliverErr(AIErrorKind::TrialExhausted,
                       detail.isNotEmpty() ? detail : "Error: Your free trial has been used up.");
            return;
        }

        deliverErr(AIErrorKind::Quota, withErrorDetail("Error: Remote provider at " + remoteHost +
                                                           " reported insufficient quota (HTTP 402)",
                                                       result.body));
        return;
    }

    if (httpStatus == 503) {
        // SERVICE_CAPACITY_EXCEEDED is a service-wide daily cap, unrelated to the caller's own
        // usage — distinct from TRIAL_EXHAUSTED and from a generic 5xx, so it gets its own kind
        // and the server's own message verbatim. Any other 503 shape falls through to the generic
        // Server mapping below.
        if (extractErrorCode(result.body) == "SERVICE_CAPACITY_EXCEEDED") {
            const auto detail = extractErrorDetail(result.body);
            deliverErr(AIErrorKind::ServiceCapacityExceeded,
                       detail.isNotEmpty() ? detail
                                           : "Error: The service is at capacity right now. Please try again shortly.");
            return;
        }
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
