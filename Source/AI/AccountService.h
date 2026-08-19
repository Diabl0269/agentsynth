#pragma once

#include "../Auth/TokenStore.h"
#include "AuthClient.h"
#include <atomic>
#include <functional>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>

namespace synth {

enum class AccountState { SignedOut, SigningIn, SignedIn };

struct AccountSnapshot {
    AccountState state = AccountState::SignedOut;
    juce::String userCode;
    juce::String verificationUriComplete;
    juce::String email;
    juce::String lastError;

    // P4-4: entitlement, fetched alongside fetchMe() on sign-in (see completeSignIn()) and
    // refreshable on demand (see refreshEntitlement()). entitlementKnown is false until the first
    // successful fetch — a fetch failure is non-fatal (mirrors how a failed fetchMe() leaves
    // `email` empty rather than failing sign-in), so callers must check it before trusting
    // plan/monthlyRequestLimit/requestsUsed rather than treating a default-constructed plan as
    // "Free".
    juce::String plan;
    int monthlyRequestLimit = 0;
    int requestsUsed = 0;
    bool entitlementKnown = false;

    // P6-7: opt-in flag for hosted-mode prompt collection used for product learning (human
    // review, never model training/fine-tuning — see docs/AI_Engine.md). Off by default, matching
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
class AccountService : private juce::Thread {
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
        signed out. Fire-and-forget — publishes an updated snapshot via onStateChanged on success,
        silent (logged, non-fatal) on failure, same contract as the entitlement fetch inside
        completeSignIn(). Intended for "the user may have just paid — check again" moments (P4-4:
        AIChatComponent calls this right after a Quota error). */
    void refreshEntitlement();

    /** Re-fetches the prompt-learning opt-in preference (P6-7) only, leaving sign-in state and
        entitlement untouched. No-op if signed out. Same fire-and-forget contract as
        refreshEntitlement(): publishes an updated snapshot on success, silent (logged, non-fatal)
        on failure. Intended for "the settings dialog just opened, or the user just signed in"
        moments — see AISettingsTab. */
    void refreshPromptLearningOptIn();

    /** Sets the prompt-learning opt-in preference (P6-7) on the server and, on success, updates
        the published snapshot to match. No-op if signed out — same gating as every other
        account-only action. Fire-and-forget: a transport failure leaves the snapshot (and
        therefore the settings checkbox) at whatever it was before the call, logged non-fatal. */
    void setPromptLearningOptIn(bool optedIn);

    // Called (via MessageManager::callAsync when it originates from the worker thread) whenever
    // getSnapshot() would return something different.
    std::function<void()> onStateChanged;

    // Called with the new access token (or an empty string on sign-out) whenever it changes.
    std::function<void(juce::String)> onAccessTokenChanged;

private:
    struct PendingJob {
        enum class Kind {
            none,
            deviceSignIn,
            silentSignIn,
            revoke,
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
    AccountSnapshot snapshot; // guarded by stateLock
    juce::String accessToken; // guarded by stateLock; in-memory only, never persisted

    // Set by cancelSignIn(), polled by the device-sign-in poll loop's wait increments and by
    // AuthClient calls made during that flow. Reset at the start of every job so a stale cancel
    // from a previous flow can't abort the next one.
    std::atomic<bool> cancelRequested{false};

    juce::CriticalSection queueLock;
    PendingJob queuedJob; // guarded by queueLock
    enum class WorkerState { idle, starting, running };
    WorkerState workerState = WorkerState::idle; // guarded by queueLock
    bool isShuttingDown = false;                 // guarded by queueLock

    void run() override;

    void runDeviceSignInFlow();
    void runSilentSignInFlow(const juce::String& storedRefreshToken);
    void runRevokeFlow(const juce::String& token);
    void runRefreshEntitlementFlow(const juce::String& accessTokenForRefresh);
    void runRefreshPromptLearningFlow(const juce::String& accessTokenForRefresh);
    void runSetPromptLearningFlow(const juce::String& accessTokenForRefresh, bool optedIn);

    /** Rotation-before-use invariant lives here: called by both flows once a fresh access/refresh
        token pair is in hand. */
    void completeSignIn(const juce::String& newAccessToken, const juce::String& newRefreshToken);

    void startJob(PendingJob::Kind kind, juce::String arg = {}, bool boolArg = false);
    bool ensureWorkerRunning();

    /** Replaces the published snapshot and notifies onStateChanged via callAsync. Normally called
        from the worker thread; startJob() also calls it directly (still via callAsync for the
        notification itself) on the rare path where the worker thread couldn't be started at all. */
    void publishSnapshot(const AccountSnapshot& newSnapshot);

    /** Updates the in-memory access token and notifies onAccessTokenChanged via callAsync (only
        ever called from the worker thread). */
    void setAccessTokenFromWorker(const juce::String& token);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AccountService)
};

} // namespace synth
