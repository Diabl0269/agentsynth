#pragma once

#include "../Auth/TokenStore.h"
#include "AuthClient.h"
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <utility>
#include <vector>

namespace synth {

enum class AccountState { SignedOut, SigningIn, SignedIn };

struct AccountSnapshot {
    AccountState state = AccountState::SignedOut;
    juce::String userCode;
    juce::String verificationUriComplete;
    juce::String email;
    juce::String lastError;

    // Entitlement, fetched alongside fetchMe() on sign-in (see completeSignIn()) and
    // refreshable on demand (see refreshEntitlement()). entitlementKnown is false until the first
    // successful fetch — a fetch failure is non-fatal (mirrors how a failed fetchMe() leaves
    // `email` empty rather than failing sign-in), so callers must check it before trusting
    // plan/monthlyRequestLimit/requestsUsed rather than treating a default-constructed plan as
    // "Free".
    juce::String plan;
    int monthlyRequestLimit = 0;
    int requestsUsed = 0;
    bool entitlementKnown = false;
    // ISO-8601 end of the current billing period, empty when the server sent none (a Free account);
    // cancelAtPeriodEnd is true once a Pro subscription is set to stop at that date.
    juce::String periodEndIso;
    bool cancelAtPeriodEnd = false;

    // Opt-in flag for hosted-mode prompt collection used for product learning (human review,
    // never training/fine-tuning — see docs/ai/feedback.md#opt-in-prompt-collection). Off by default, matching
    // the server's default, so a default-constructed/not-yet-fetched snapshot is indistinguishable
    // from "known opted out" — same reasoning as `plan`'s empty-string default above, and safe
    // here for the same reason: a settings checkbox reading this before the first fetch shows
    // unchecked, never a stale opted-in. promptLearningOptInAt is the server's ISO-8601
    // `opted_in_at`, empty when never opted in.
    bool promptLearningOptIn = false;
    juce::String promptLearningOptInAt;
};

/**
 * @brief True only for an exact case-insensitive "pro" plan. Deliberately does NOT check
 *        entitlementKnown: a default-constructed/entitlement-not-yet-fetched snapshot has
 *        `plan` at its default value (empty string), which already fails the comparison — see
 *        AccountSnapshot::entitlementKnown's doc comment. Callers that need to distinguish
 *        "known Free" from "not yet known" should still check entitlementKnown separately;
 *        this helper only answers "should this session behave as Pro right now."
 */
inline bool isProPlan(const AccountSnapshot& snapshot) { return snapshot.plan.equalsIgnoreCase("pro"); }

/**
 * @class AccountService
 * @brief The account state machine: owns one AuthClient, one TokenStore, and a single background
 *        worker thread that runs at most one flow at a time (device sign-in, a silent
 *        refresh-token sign-in, or a best-effort revoke after sign-out).
 *
 * Mirrors RemoteProvider's worker-thread/queue discipline (queueLock-guarded state, an
 * idle/starting/running WorkerState, callbacks dispatched via juce::MessageManager::callAsync when
 * they originate from the worker thread) but simplified: there is only ever one job slot, not a
 * queue of many, because the UI can only ever be running one flow at a time.
 */
class AccountService
    : private juce::Thread
    , private juce::Timer {
public:
    explicit AccountService(juce::String host = "http://localhost:8787");

    // Test-specific constructor: injects a fake HttpPerformer (no real sockets) and an arbitrary
    // TokenStore (typically InMemoryTokenStore, so tests never touch the real Keychain).
    AccountService(juce::String host, AuthClient::HttpPerformer performer, std::unique_ptr<TokenStore> tokenStoreIn);

    ~AccountService() override;

    /** Thread-safe copy of the current state. */
    AccountSnapshot getSnapshot() const;

    /** In-memory-only access token, never persisted. Empty when signed out. */
    juce::String getAccessToken() const;

    /** Startup path: loads a stored refresh token (if any) and tries to redeem it. Never surfaces
        an error — an absent, expired, or revoked-elsewhere token just leaves the app SignedOut,
        the same as if the user had never signed in. Offline at startup is likewise silent. */
    void attemptSilentSignIn();

    /** Starts the device-code flow on the worker thread. */
    void beginSignIn();

    /** Cooperatively cancels an in-flight beginSignIn() flow. No-op if nothing is signing in.
        Also guards the narrow window after a poll has already succeeded but before the resulting
        tokens are persisted/published — see completeSignIn()'s cancellation check. */
    void cancelSignIn();

    /** Clears the TokenStore and the in-memory access token immediately (synchronously, on the
        calling thread) and notifies listeners right away — does not wait on the network. Also
        interrupts any in-flight sign-in flow (the same guard cancelSignIn() relies on), so a
        sign-in that's mid-flight when this is called cannot complete afterward and silently sign
        the user back in. A best-effort revoke of the (now-discarded) refresh token is then fired
        on the worker thread; its result is ignored either way. */
    void signOut();

    /** Re-fetches entitlement only (plan/limit/usage), leaving sign-in state untouched. No-op if
        signed out. Fire-and-forget — publishes an updated snapshot to the state listeners on success,
        silent (logged, non-fatal) on failure, same contract as the entitlement fetch inside
        completeSignIn(). Intended for "the user may have just paid — check again" moments
        (AIChatComponent calls this right after a Quota error). */
    void refreshEntitlement();

    /** Re-fetches the prompt-learning opt-in preference only, leaving sign-in state and
        entitlement untouched. No-op if signed out. Same fire-and-forget contract as
        refreshEntitlement(): publishes an updated snapshot on success, silent (logged, non-fatal)
        on failure. Intended for "the settings dialog just opened, or the user just signed in"
        moments — see AISettingsTab. */
    void refreshPromptLearningOptIn();

    /** Sets the prompt-learning opt-in preference on the server and, on success, updates
        the published snapshot to match. No-op if signed out — same gating as every other
        account-only action. Fire-and-forget: a transport failure leaves the snapshot (and
        therefore the settings checkbox) at whatever it was before the call, logged non-fatal. */
    void setPromptLearningOptIn(bool optedIn);

    /** What a refresh attempt ended in. */
    enum class RefreshOutcome {
        Refreshed,    // `accessToken` is a valid, current token (this call's or one a parallel call just got)
        SessionEnded, // the server no longer accepts the refresh token (revoked, expired, reused) or none is stored:
                      // the service is SignedOut with a "sign in again" message
        Unavailable   // could not tell (offline, server error, a sign-in in flight): keep the session, try later
    };
    struct RefreshResult {
        RefreshOutcome outcome = RefreshOutcome::Unavailable;
        juce::String accessToken;
    };

    /** Single-flight refresh, for a caller whose request was answered 401 with `rejectedToken` (or empty to refresh
        unconditionally). Blocks the calling thread for one network round trip, so call it from a background thread,
        never the message thread. Concurrent callers queue behind one mutex: the first redeems the refresh token, the
        rest see a newer access token than the one they were rejected with and return it without a second call (the
        refresh token rotates and a reused one revokes the whole family, docs/ai/accounts.md#expiry-and-refresh).
        The rotated refresh token is persisted before the new access token is exposed. */
    RefreshResult refreshAccessToken(const juce::String& rejectedToken = {});

    /** The access token to send now: refreshes first (same single-flight path) when it is within five minutes of
        expiry. Background threads only, like refreshAccessToken(). Empty when signed out. */
    juce::String ensureFreshToken();

    /** The token for a request about to be sent from the message thread: getAccessToken(), after queueing a background
        refresh when it is near expiry (never blocks). Use this, not getAccessToken(), wherever a request starts. */
    juce::String accessTokenForRequest();

    /** Queues a background refresh when the access token is within five minutes of expiry. The 30 s timer calls it;
        safe from any thread. */
    void refreshIfDue();

    /** Test seam: the clock (milliseconds since the epoch) the expiry maths reads. */
    void setClockForTesting(std::function<juce::int64()> clock);

    using ListenerId = int;

    /** Registers `callback` to run (via MessageManager::callAsync when the change originates from
        the worker thread) whenever getSnapshot() would return something different. Any number of
        listeners may be registered; each is called in registration order. Returns the id to pass
        to removeStateListener(). A notification already queued when a listener is removed still
        runs it, so a subscriber captures a SafePointer (or otherwise tolerates a late call). */
    ListenerId addStateListener(std::function<void()> callback);
    void removeStateListener(ListenerId id);

    /** Same, for the access token: `callback` receives the new token (an empty string on
        sign-out) whenever it changes. */
    ListenerId addAccessTokenListener(std::function<void(juce::String)> callback);
    void removeAccessTokenListener(ListenerId id);

private:
    struct PendingJob {
        enum class Kind {
            none,
            deviceSignIn,
            silentSignIn,
            revoke,
            refreshSession,
            refreshEntitlement,
            refreshPromptLearning,
            setPromptLearningOptIn
        };
        Kind kind = Kind::none;
        // stored refresh token (silentSignIn), token to revoke (revoke), or the access token to
        // refresh entitlement/prompt-learning preference for (refreshEntitlement,
        // refreshPromptLearning, setPromptLearningOptIn)
        juce::String arg;
        // Only meaningful for setPromptLearningOptIn: the new opted-in value to PUT. A second
        // field rather than encoding it into `arg` (which setPromptLearningOptIn also needs, for
        // the access token) or a second Kind pair (setPromptLearningOptInTrue/False) — this is the
        // minimal extension that keeps `arg`'s existing meaning intact for every other job kind.
        bool boolArg = false;
    };

    AuthClient authClient;
    std::unique_ptr<TokenStore> tokenStore;

    mutable juce::CriticalSection stateLock;
    AccountSnapshot snapshot;             // guarded by stateLock
    juce::String accessToken;             // guarded by stateLock; in-memory only, never persisted
    juce::int64 accessExpiresAtMs = 0;    // guarded by stateLock; 0 = unknown (the server sent no expires_in)
    std::function<juce::int64()> clockMs; // guarded by stateLock; null = the system clock
    std::mutex refreshMutex;              // single-flight: one refresh-token redemption at a time

    // Set by cancelSignIn(), polled by the device-sign-in poll loop's wait increments and by
    // AuthClient calls made during that flow. Reset at the start of every job so a stale cancel
    // from a previous flow can't abort the next one.
    std::atomic<bool> cancelRequested{false};

    juce::CriticalSection queueLock;
    PendingJob queuedJob; // guarded by queueLock
    enum class WorkerState { idle, starting, running };
    WorkerState workerState = WorkerState::idle; // guarded by queueLock
    bool isShuttingDown = false;                 // guarded by queueLock
    bool jobRunning = false;                     // guarded by queueLock; the worker is inside a job

    void run() override;

    void runDeviceSignInFlow();
    void runSilentSignInFlow(const juce::String& storedRefreshToken);
    void runRevokeFlow(const juce::String& token);
    void runRefreshEntitlementFlow(const juce::String& accessTokenForRefresh);
    void runRefreshPromptLearningFlow(const juce::String& accessTokenForRefresh);
    void runSetPromptLearningFlow(const juce::String& accessTokenForRefresh, bool optedIn);

    /** Rotation-before-use invariant lives here: called by both flows once a fresh access/refresh
        token pair is in hand. */
    void completeSignIn(const juce::String& newAccessToken, const juce::String& newRefreshToken, int expiresInSeconds);
    void runRefreshSessionFlow();
    void timerCallback() override;
    void startExpiryTimer();
    juce::int64 nowMsLocked() const; // caller holds stateLock
    bool accessTokenDue() const;
    void endSession(const juce::String& logReason);

    void startJob(PendingJob::Kind kind, juce::String arg = {}, bool boolArg = false, bool onlyIfIdle = false);
    bool ensureWorkerRunning();

    /** Replaces the published snapshot and notifies the state listeners via callAsync. Normally called
        from the worker thread; startJob() also calls it directly (still via callAsync for the
        notification itself) on the rare path where the worker thread couldn't be started at all. */
    void publishSnapshot(const AccountSnapshot& newSnapshot);

    /** Updates the in-memory access token and notifies the access-token listeners via callAsync (only
        ever called from the worker thread). */
    void setAccessTokenFromWorker(const juce::String& token, int expiresInSeconds = 0);

    mutable juce::CriticalSection listenerLock;
    std::vector<std::pair<ListenerId, std::function<void()>>> stateListeners;             // guarded by listenerLock
    std::vector<std::pair<ListenerId, std::function<void(juce::String)>>> tokenListeners; // guarded by listenerLock
    ListenerId nextListenerId = 1;                                                        // guarded by listenerLock

    /** Copies the listeners under the lock, so a caller can run them (or queue them) unlocked. */
    std::vector<std::function<void()>> copyStateListeners() const;
    std::vector<std::function<void(juce::String)>> copyTokenListeners() const;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccountService)
};

} // namespace synth
