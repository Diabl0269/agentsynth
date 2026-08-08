#pragma once

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>

namespace synth {

/**
 * @class AuthClient
 * @brief Synchronous, stateless protocol client for the synth-platform auth service's device-code
 *        OAuth flow (`POST {host}/v1/auth/...`).
 *
 * Deliberately NOT threaded and NOT callback-based: every method blocks the calling thread and
 * returns a plain result struct, no exceptions. AccountService is the one thing that calls these
 * methods off the message thread (from its own worker thread) and turns them into the async,
 * notified state machine callers actually see.
 *
 * Mirrors RemoteProvider's HTTP transport shape (see Source/AI/RemoteProvider.h/.cpp): a real
 * libcurl-backed HttpPerformer installed by the production constructor, a second constructor that
 * injects a fake HttpPerformer for tests (no real sockets), and the same
 * `#ifndef _WIN32` real curl / `#else` stub split. This is a deliberately separate copy of the
 * HttpPerformer/HttpResult shape (RemoteProvider.h is not touched), extended with an explicit
 * `method` ("GET" or "POST") since, unlike RemoteProvider (POST-only), this client also needs
 * GET /v1/auth/me.
 */
class AuthClient {
public:
    /** Result of one HTTP transaction, transport-layer only — no interpretation of the body. Same
        shape as RemoteProvider::HttpResult. */
    struct HttpResult {
        int httpStatus = 0;
        juce::String body;
        juce::StringPairArray headers;
        bool transportFailed = false; // curl couldn't complete the transfer at all (DNS/connect/etc.)
        bool timedOut = false;
        juce::String errorMessage; // human-readable, meaningful when transportFailed or timedOut
    };

    /** Performs one HTTP request. `cancelled` is polled by the implementation (real or fake) so a
        request can be aborted mid-transfer. `method` is "GET" or "POST"; `body` is ignored for GET
        and is the raw (already form-encoded) request body for POST. */
    using HttpPerformer = std::function<HttpResult(
        const juce::String& method, const juce::String& url, const juce::StringPairArray& requestHeaders,
        const juce::String& body, int timeoutMs, const std::atomic<bool>& cancelled)>;

    /** Result of POST /v1/auth/device/code. */
    struct DeviceCodeResult {
        bool ok = false;
        juce::String deviceCode;
        juce::String userCode;
        juce::String verificationUri;
        juce::String verificationUriComplete;
        int expiresIn = 0;
        int interval = 0;
        juce::String transportError; // non-empty on transport failure or an unparseable/unexpected response
    };

    /** Result of POST /v1/auth/token, for both the device-code poll and the refresh-token grant. */
    struct TokenPollResult {
        bool ok = false;
        juce::String errorCode; // e.g. "authorization_pending", "slow_down", "invalid_grant"; empty on success
        juce::String errorDescription;
        juce::String accessToken;
        int expiresIn = 0;
        juce::String refreshToken;
        juce::String transportError; // non-empty on transport failure; distinct from a well-formed error response
    };

    /** Result of GET /v1/auth/me. */
    struct MeResult {
        bool ok = false;
        juce::String id;
        juce::String email;
        juce::String displayName;
        juce::String transportError;
    };

    explicit AuthClient(juce::String host = "http://localhost:8787", juce::String clientId = "synth-desktop");

    // Test-specific constructor to inject a fake HTTP transport (no real sockets).
    AuthClient(juce::String host, juce::String clientId, HttpPerformer performer);

    /** POST /v1/auth/device/code — starts a device-code flow. */
    DeviceCodeResult requestDeviceCode(const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/token with the device-code grant. Polled repeatedly by the caller until it
        succeeds or returns a terminal error. */
    TokenPollResult pollDeviceToken(const juce::String& deviceCode, const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/token with the refresh-token grant. */
    TokenPollResult refreshToken(const juce::String& refreshTokenValue, const std::atomic<bool>& cancelled) const;

    /** GET /v1/auth/me with `Authorization: Bearer <accessToken>`. */
    MeResult fetchMe(const juce::String& accessToken, const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/revoke. Fire-and-forget: the endpoint always answers 200 with an empty body,
        so the return value only reflects whether the transport succeeded — callers are not
        expected to act on it either way. */
    bool revoke(const juce::String& token, const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/logout with `Authorization: Bearer <accessToken>`, no body. Same
        fire-and-forget contract as revoke(). */
    bool logout(const juce::String& accessToken, const std::atomic<bool>& cancelled) const;

private:
    juce::String host;
    juce::String clientId;
    HttpPerformer performHttp;

    /** Shared implementation for pollDeviceToken()/refreshToken(): both hit the same endpoint and
        parse the same response shapes, differing only in the request body. */
    TokenPollResult postToken(const juce::String& formBody, const std::atomic<bool>& cancelled) const;
};

} // namespace synth
