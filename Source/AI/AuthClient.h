#pragma once

#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <vector>

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

    /** Result of GET /v1/entitlement (P4-2/P4-3/P4-4). `requestsUsed`/`usagePeriodStartIso` come
        from the response's `usage` object; both stay at their defaults (0 / empty) against a
        server that doesn't send it yet — see docs/billing.md for the full response shape. */
    struct EntitlementResult {
        bool ok = false;
        juce::String plan;
        juce::String status;
        juce::String periodEndIso;
        bool cancelAtPeriodEnd = false;
        int monthlyRequestLimit = 0;
        int requestsUsed = 0;
        juce::String usagePeriodStartIso;
        juce::String transportError;
    };

    /** Result of GET/PUT /v1/prompt-learning (P6-7: opt-in prompt collection for product
        learning). `optedInAt` is the server's ISO-8601 `opted_in_at`, empty when it sent null
        (never opted in, or opted out again). */
    struct PromptLearningPreferenceResult {
        bool ok = false;
        bool optedIn = false;
        juce::String optedInAt;
        juce::String transportError;
    };

    /** One entry in GET /v1/conversations' `data.conversations` array, and the summary fields of
        GET /v1/conversations/:id's `data.conversation`. */
    struct ConversationSummary {
        juce::String id;
        juce::String title; // empty when the server sent null (untitled conversation)
        juce::String createdAt;
        juce::String updatedAt;
    };

    /** Result of GET /v1/conversations (P6-8). Cloud-only — conversation persistence itself is
        Pro-gated server-side; this client has no plan awareness of its own, same as every other
        AuthClient method. */
    struct ListConversationsResult {
        bool ok = false;
        std::vector<ConversationSummary> conversations;
        // `data.deletionScheduledAt` — empty when the server sent null (no grace-period deletion
        // pending). NOTE: this endpoint writes on read server-side — the first list call after a
        // plan lapse is what sets this date — so calling listConversations() is not a
        // side-effect-free read. See docs/AI_Engine.md.
        juce::String deletionScheduledAt;
        juce::String transportError;
    };

    /** One message in a conversation's `messages` array (GET /v1/conversations/:id). */
    struct ConversationMessage {
        juce::String role; // "user" or "assistant"
        juce::String content;
        juce::String createdAt;
    };

    /** Result of GET /v1/conversations/:id. Flattens `data.conversation`'s summary fields
        alongside its `messages` array, rather than nesting a ConversationSummary, so a caller
        that only wants messages doesn't have to reach through an extra level. */
    struct ConversationDetailResult {
        bool ok = false;
        juce::String id;
        juce::String title;
        juce::String createdAt;
        juce::String updatedAt;
        std::vector<ConversationMessage> messages;
        juce::String transportError;
    };

    /** Result of DELETE /v1/conversations/:id. */
    struct DeleteConversationResult {
        bool ok = false;
        bool deleted = false; // `data.deleted` — parroted back rather than assumed from `ok`
        juce::String transportError;
    };

    /** Result of DELETE /v1/conversations (GDPR erasure — every conversation owned by the
        signed-in account). */
    struct DeleteAllConversationsResult {
        bool ok = false;
        int deletedCount = 0;
        juce::String transportError;
    };

    /** Result of POST /v1/conversations/:conversationId/messages/:messageId/feedback (P6-9). No
        response body worth parsing on success — `ok` is the whole story. */
    struct SubmitMessageFeedbackResult {
        bool ok = false;
        juce::String transportError;
    };

    /** Result of POST /v1/feedback (P6-16). No response body worth parsing on success -- `ok` is
        the whole story, same shape as SubmitMessageFeedbackResult. */
    struct SubmitGeneralFeedbackResult {
        bool ok = false;
        juce::String transportError;
    };

    explicit AuthClient(juce::String host = "http://localhost:8787", juce::String clientId = "synth-desktop",
                        juce::String deviceId = "");

    // Test-specific constructor to inject a fake HTTP transport (no real sockets). `deviceId`
    // defaults to empty (omitted from requests) so existing 3-arg call sites keep compiling.
    AuthClient(juce::String host, juce::String clientId, HttpPerformer performer, juce::String deviceId = "");

    /** POST /v1/auth/device/code — starts a device-code flow. */
    DeviceCodeResult requestDeviceCode(const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/token with the device-code grant. Polled repeatedly by the caller until it
        succeeds or returns a terminal error. */
    TokenPollResult pollDeviceToken(const juce::String& deviceCode, const std::atomic<bool>& cancelled) const;

    /** POST /v1/auth/token with the refresh-token grant. */
    TokenPollResult refreshToken(const juce::String& refreshTokenValue, const std::atomic<bool>& cancelled) const;

    /** GET /v1/auth/me with `Authorization: Bearer <accessToken>`. */
    MeResult fetchMe(const juce::String& accessToken, const std::atomic<bool>& cancelled) const;

    /** GET /v1/entitlement with `Authorization: Bearer <accessToken>`. */
    EntitlementResult fetchEntitlement(const juce::String& accessToken, const std::atomic<bool>& cancelled) const;

    /** GET /v1/prompt-learning with `Authorization: Bearer <accessToken>` (P6-7). */
    PromptLearningPreferenceResult fetchPromptLearningPreference(const juce::String& accessToken,
                                                                 const std::atomic<bool>& cancelled) const;

    /** PUT /v1/prompt-learning with `Authorization: Bearer <accessToken>` and a JSON
        `{"opted_in": optedIn}` body (P6-7). Toggling to false purges the server's accumulated
        samples for this user; that purge is a server-side concern, nothing further for the client
        to do. */
    PromptLearningPreferenceResult setPromptLearningPreference(const juce::String& accessToken, bool optedIn,
                                                               const std::atomic<bool>& cancelled) const;

    /** GET /v1/conversations with `Authorization: Bearer <accessToken>` (P6-8). See
        ListConversationsResult's doc comment for the read-writes-deletionScheduledAt caveat. */
    ListConversationsResult listConversations(const juce::String& accessToken,
                                              const std::atomic<bool>& cancelled) const;

    /** GET /v1/conversations/:id with `Authorization: Bearer <accessToken>`. */
    ConversationDetailResult getConversation(const juce::String& accessToken, const juce::String& conversationId,
                                             const std::atomic<bool>& cancelled) const;

    /** DELETE /v1/conversations/:id with `Authorization: Bearer <accessToken>`. */
    DeleteConversationResult deleteConversation(const juce::String& accessToken, const juce::String& conversationId,
                                                const std::atomic<bool>& cancelled) const;

    /** DELETE /v1/conversations with `Authorization: Bearer <accessToken>` — GDPR erasure of
        every conversation owned by the signed-in account. */
    DeleteAllConversationsResult deleteAllConversations(const juce::String& accessToken,
                                                        const std::atomic<bool>& cancelled) const;

    /** POST /v1/conversations/:conversationId/messages/:messageId/feedback with
        `Authorization: Bearer <accessToken>` and a JSON `{"rating": rating, "comment"?: comment}`
        body (P6-9). `rating` is "up" or "down"; `comment` is omitted from the body entirely when
        empty, same convention as PatchFeedbackStore::record()'s local log. Server-side this is
        Pro-gated (403 for a non-Pro account) and returns 404 when the conversation/message id is
        wrong or not owned by this user — this client has no plan awareness of its own (same as
        every other AuthClient method), so callers are expected to gate on Pro before calling. */
    SubmitMessageFeedbackResult submitMessageFeedback(const juce::String& accessToken,
                                                      const juce::String& conversationId, const juce::String& messageId,
                                                      const juce::String& rating, const juce::String& comment,
                                                      const std::atomic<bool>& cancelled) const;

    /** POST /v1/feedback with a JSON `{"category": category, "text": text}` body (P6-16).
        `category` is "bug", "feature", or "other". Unlike submitMessageFeedback (P6-9), the server
        does NOT plan-gate this -- any account, signed in or not, may submit general feedback.
        `Authorization: Bearer <accessToken>` is set only when `accessToken` is non-empty; the
        `X-Device-Id` header is always set from this AuthClient's own device id (see `deviceId`
        below) when non-empty, so a signed-out caller still gets attributed feedback via the
        device's stable anonymous id (P6-17). Callers pass an empty `accessToken` to submit
        anonymously. */
    SubmitGeneralFeedbackResult submitGeneralFeedback(const juce::String& accessToken, const juce::String& category,
                                                      const juce::String& text,
                                                      const std::atomic<bool>& cancelled) const;

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
    // A stable per-install identifier (see Source/Auth/DeviceIdStore.h), sent as the `device_id`
    // form field on device-code issuance and token exchange whenever non-empty. NOT a secret —
    // unlike clientId/HttpPerformer this is optional: an empty string just omits the field, which
    // is what every pre-existing 3-arg constructor call (all current callers) gets.
    juce::String deviceId;
    HttpPerformer performHttp;

    /** Shared implementation for pollDeviceToken()/refreshToken(): both hit the same endpoint and
        parse the same response shapes, differing only in the request body. */
    TokenPollResult postToken(const juce::String& formBody, const std::atomic<bool>& cancelled) const;
};

} // namespace synth
