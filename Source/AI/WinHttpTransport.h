#pragma once

#include <atomic>
#include <juce_core/juce_core.h>

namespace synth {

/** Outcome of one WinHttpTransport request. Same semantics as RemoteProvider::HttpResult and
    AuthClient::HttpResult (both are filled from this): httpStatus is 0 unless a response arrived;
    transportFailed means no complete response (DNS/connect/TLS/cancel); timedOut means the total
    time budget ran out; errorMessage is human-readable when either flag is set. */
struct WinHttpResult {
    int httpStatus = 0;
    juce::String body;
    juce::StringPairArray headers;
    bool transportFailed = false;
    bool timedOut = false;
    juce::String errorMessage;
};

/**
 * One blocking HTTP request over WinHTTP, the Windows counterpart of the libcurl performers in
 * RemoteProvider.cpp and AuthClient.cpp (no third-party dependency; WinHTTP ships with Windows
 * and honours the system proxy settings).
 *
 * `method` is "GET", "POST", "PUT", "DELETE", ...; `url` is http or https with any port;
 * `jsonBody` is sent verbatim as UTF-8 (pass an empty string for none). `timeoutMs` bounds the
 * WHOLE request, not each step. `cancelled` is observed while the request is blocked in the
 * network (not only between reads), so setting it aborts promptly with transportFailed and the
 * message "Request aborted (cancelled).". Redirects are not followed, like the curl performers.
 * Call from any thread; each call owns its own handles.
 *
 * Declared on every platform so callers need no #ifdef of their own beyond the call site, but
 * only defined on Windows (WinHttpTransport.cpp).
 */
WinHttpResult performWinHttpRequest(const juce::String& method, const juce::String& url,
                                    const juce::StringPairArray& requestHeaders, const juce::String& body,
                                    int timeoutMs, const std::atomic<bool>& cancelled);

} // namespace synth
