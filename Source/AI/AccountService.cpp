#include "AccountService.h"
#include "../Auth/DeviceIdStore.h"
#include "../Auth/KeychainTokenStore.h"

namespace synth {

namespace {

// Purely a lost-wakeup safety net (see RemoteProvider's identical kIdleWaitMs), never relied on:
// notify() wakes the worker immediately whenever a job is queued or a poll wait should end early.
constexpr int kIdleWaitMs = 1000;

// Upper bound on waiting for a retired worker's thread handle to be released so a replacement can
// start. See RemoteProvider's kWorkerHandoverTimeoutMs.
constexpr int kWorkerHandoverTimeoutMs = 2000;

// The device-sign-in poll loop sleeps for `interval` seconds between polls, but in these short
// increments so cancelSignIn() is noticed promptly instead of blocking up to `interval` seconds.
constexpr int kPollWaitChunkMs = 250;

const char* const kAuthorizationPending = "authorization_pending";
const char* const kSlowDown = "slow_down";

/** "j***@example.com" — enough to confirm in a log which account signed in without writing a
    full email address to disk. Falls back to a fixed placeholder for anything that isn't a
    plausible `x@y` shape. */
juce::String maskEmail(const juce::String& email) {
    const int at = email.indexOfChar('@');
    if (at <= 0)
        return "(no email)";
    return email.substring(0, 1) + "***" + email.substring(at);
}

} // namespace

AccountService::AccountService(juce::String host)
    : Thread("AccountServiceThread")
    // DeviceIdStore() (no explicit path) resolves to the app's standard settings-folder
    // convention and persists on first read — see Source/Auth/DeviceIdStore.h. A stable id, not
    // a secret, so reading/generating it here (rather than caching a member) is fine: every call
    // against the same file returns the same value.
    , authClient(std::move(host), "synth-desktop", DeviceIdStore().getDeviceId())
    , tokenStore(std::make_unique<KeychainTokenStore>()) {}

AccountService::AccountService(juce::String host, AuthClient::HttpPerformer performer,
                               std::unique_ptr<TokenStore> tokenStoreIn)
    : Thread("AccountServiceThread")
    , authClient(std::move(host), "synth-desktop", std::move(performer))
    , tokenStore(std::move(tokenStoreIn)) {}

AccountService::~AccountService() {
    {
        const juce::ScopedLock sl(queueLock);
        isShuttingDown = true;
    }
    cancelRequested.store(true);
    stopThread(2000);
}

AccountSnapshot AccountService::getSnapshot() const {
    const juce::ScopedLock sl(stateLock);
    return snapshot;
}

juce::String AccountService::getAccessToken() const {
    const juce::ScopedLock sl(stateLock);
    return accessToken;
}

void AccountService::attemptSilentSignIn() {
    const juce::String stored = tokenStore->load();
    if (stored.isEmpty())
        return; // Nothing to do: stay SignedOut, exactly the same as a user who never signed in.

    startJob(PendingJob::Kind::silentSignIn, stored);
}

void AccountService::beginSignIn() { startJob(PendingJob::Kind::deviceSignIn); }

void AccountService::cancelSignIn() {
    cancelRequested.store(true);
    notify(); // wakes the poll loop's wait() early instead of leaving it to time out
}

void AccountService::signOut() {
    // Interrupts any in-flight sign-in (device or silent) that might otherwise complete after
    // this point and persist a token into the store this call is about to clear — see
    // completeSignIn()'s cancellation check for the other half of this guard. A stale cancel left
    // over for the *next* job is not a concern: run()'s dispatch loop resets it before invoking
    // whatever runs next (including the revoke job queued below).
    cancelRequested.store(true);
    notify();

    // Captured before clearing so the best-effort revoke below still has something to revoke.
    const juce::String refreshTokenToRevoke = tokenStore->load();

    {
        const juce::ScopedLock sl(stateLock);
        accessToken.clear();
        snapshot = AccountSnapshot{}; // back to SignedOut with every field reset
    }
    tokenStore->clear();

    // signOut() runs on the calling thread (never the worker thread), so these are called
    // directly rather than via callAsync — "immediately" per the class's contract, not deferred
    // until the best-effort revoke below completes.
    if (onAccessTokenChanged)
        onAccessTokenChanged({});
    if (onStateChanged)
        onStateChanged();

    juce::Logger::writeToLog("AccountService: signed out.");

    if (refreshTokenToRevoke.isNotEmpty())
        startJob(PendingJob::Kind::revoke, refreshTokenToRevoke);
}

void AccountService::refreshEntitlement() {
    const juce::String token = getAccessToken();
    if (token.isEmpty())
        return; // signed out — nothing to refresh
    startJob(PendingJob::Kind::refreshEntitlement, token);
}

void AccountService::startJob(PendingJob::Kind kind, juce::String arg) {
    bool mustStart = false;
    {
        const juce::ScopedLock sl(queueLock);
        if (isShuttingDown)
            return;

        queuedJob = PendingJob{kind, std::move(arg)};

        const bool ownerVanished = (workerState == WorkerState::running && !isThreadRunning());
        mustStart = (workerState == WorkerState::idle) || ownerVanished;
        if (mustStart)
            workerState = WorkerState::starting;
    }

    if (mustStart && !ensureWorkerRunning()) {
        {
            const juce::ScopedLock sl(queueLock);
            workerState = WorkerState::idle;
            queuedJob = PendingJob{};
        }
        juce::Logger::writeToLog("AccountService: could not start the worker thread.");

        // Interactive sign-in deserves visible feedback; a failed silent-sign-in or revoke stays
        // silent/best-effort per their existing contracts (an unreachable worker thread is exactly
        // as inert as an unreachable network).
        if (kind == PendingJob::Kind::deviceSignIn) {
            AccountSnapshot s;
            s.state = AccountState::SignedOut;
            s.lastError = "Error: could not start sign-in.";
            publishSnapshot(s);
        }
        return;
    }

    notify();
}

bool AccountService::ensureWorkerRunning() {
    if (startThread())
        return true;

    if (juce::Thread::getCurrentThreadId() == getThreadId())
        return false;

    if (!waitForThreadToExit(kWorkerHandoverTimeoutMs))
        return false;

    return startThread();
}

void AccountService::run() {
    {
        const juce::ScopedLock sl(queueLock);
        workerState = WorkerState::running;
    }

    for (;;) {
        PendingJob job;
        {
            const juce::ScopedLock sl(queueLock);
            if (threadShouldExit())
                break;

            if (queuedJob.kind != PendingJob::Kind::none) {
                job = queuedJob;
                queuedJob = PendingJob{};
            }
        }

        if (job.kind != PendingJob::Kind::none) {
            cancelRequested.store(false); // a stale cancel from a previous flow must not leak in

            switch (job.kind) {
            case PendingJob::Kind::deviceSignIn:
                runDeviceSignInFlow();
                break;
            case PendingJob::Kind::silentSignIn:
                runSilentSignInFlow(job.arg);
                break;
            case PendingJob::Kind::revoke:
                runRevokeFlow(job.arg);
                break;
            case PendingJob::Kind::refreshEntitlement:
                runRefreshEntitlementFlow(job.arg);
                break;
            case PendingJob::Kind::none:
                break;
            }
            continue;
        }

        wait(kIdleWaitMs);
    }

    const juce::ScopedLock sl(queueLock);
    workerState = WorkerState::idle;
}

void AccountService::runDeviceSignInFlow() {
    juce::Logger::writeToLog("AccountService: starting device sign-in flow.");

    const auto codeResult = authClient.requestDeviceCode(cancelRequested);
    if (!codeResult.ok) {
        AccountSnapshot s;
        s.state = AccountState::SignedOut;
        s.lastError = codeResult.transportError.isNotEmpty() ? codeResult.transportError
                                                             : juce::String("Error: could not start sign-in.");
        publishSnapshot(s);
        juce::Logger::writeToLog("AccountService: device sign-in failed to start: " + s.lastError);
        return;
    }

    {
        AccountSnapshot s;
        s.state = AccountState::SigningIn;
        s.userCode = codeResult.userCode;
        s.verificationUriComplete = codeResult.verificationUriComplete;
        publishSnapshot(s);
    }

    // Trusts the service's interval as-is (including 0, which some test doubles use to keep
    // polling immediate) rather than clamping to a minimum — a real device-code service always
    // sends a sane value (typically several seconds).
    int intervalSeconds = juce::jmax(0, codeResult.interval);
    const juce::String deviceCode = codeResult.deviceCode;

    for (;;) {
        // Sleep for intervalSeconds, but in short chunks so cancelSignIn() is responsive instead
        // of blocking up to intervalSeconds.
        int waitedMs = 0;
        const int totalWaitMs = intervalSeconds * 1000;
        while (waitedMs < totalWaitMs) {
            if (threadShouldExit() || cancelRequested.load())
                break;
            const int chunkMs = juce::jmin(kPollWaitChunkMs, totalWaitMs - waitedMs);
            wait(chunkMs);
            waitedMs += chunkMs;
        }

        if (threadShouldExit() || cancelRequested.load()) {
            AccountSnapshot s; // SignedOut, no error: a cancelled sign-in is not a failure
            publishSnapshot(s);
            juce::Logger::writeToLog("AccountService: device sign-in cancelled.");
            return;
        }

        const auto poll = authClient.pollDeviceToken(deviceCode, cancelRequested);

        if (poll.transportError.isNotEmpty()) {
            AccountSnapshot s;
            s.state = AccountState::SignedOut;
            s.lastError = poll.transportError;
            publishSnapshot(s);
            juce::Logger::writeToLog("AccountService: device sign-in failed: " + s.lastError);
            return;
        }

        if (poll.ok) {
            completeSignIn(poll.accessToken, poll.refreshToken);
            return;
        }

        if (poll.errorCode == kAuthorizationPending)
            continue;

        if (poll.errorCode == kSlowDown) {
            intervalSeconds += 5;
            juce::Logger::writeToLog("AccountService: slow_down received, poll interval now " +
                                     juce::String(intervalSeconds) + "s.");
            continue;
        }

        // expired_token, access_denied, invalid_grant, invalid_request: terminal error.
        AccountSnapshot s;
        s.state = AccountState::SignedOut;
        s.lastError = poll.errorDescription.isNotEmpty() ? poll.errorDescription : poll.errorCode;
        publishSnapshot(s);
        juce::Logger::writeToLog("AccountService: device sign-in failed (" + poll.errorCode + ").");
        return;
    }
}

void AccountService::runSilentSignInFlow(const juce::String& storedRefreshToken) {
    juce::Logger::writeToLog("AccountService: attempting silent sign-in.");

    const auto result = authClient.refreshToken(storedRefreshToken, cancelRequested);

    if (result.transportError.isNotEmpty()) {
        // Offline (or the service is down) at startup is not an error to surface — stay
        // SignedOut silently, same as if attemptSilentSignIn() had found nothing stored.
        juce::Logger::writeToLog("AccountService: silent sign-in skipped (transport failure).");
        return;
    }

    if (!result.ok) {
        // A session revoked elsewhere, or a token that has simply expired, looks like
        // invalid_grant here — clear the dead token so the app stops trying it every launch.
        // Either way this is silent: a user who never signed in, or whose session ended
        // elsewhere, shouldn't see a scary error on every app launch.
        if (result.errorCode == "invalid_grant")
            tokenStore->clear();

        juce::Logger::writeToLog("AccountService: silent sign-in declined (" + result.errorCode +
                                 "), staying signed out.");
        return;
    }

    completeSignIn(result.accessToken, result.refreshToken);
}

void AccountService::runRevokeFlow(const juce::String& token) {
    // Fire-and-forget per the API's "always 200" contract: the result is intentionally ignored.
    authClient.revoke(token, cancelRequested);
}

void AccountService::runRefreshEntitlementFlow(const juce::String& accessTokenForRefresh) {
    if (threadShouldExit() || cancelRequested.load())
        return;

    const auto entitlement = authClient.fetchEntitlement(accessTokenForRefresh, cancelRequested);
    if (!entitlement.ok) {
        juce::Logger::writeToLog("AccountService: refreshEntitlement failed (non-fatal): " +
                                 entitlement.transportError);
        return;
    }

    // Merge onto the currently published snapshot rather than a fresh AccountSnapshot{} — this
    // job only ever touches entitlement fields, never sign-in state. If a sign-out raced this job
    // (the snapshot is no longer SignedIn by the time the fetch returns), publishing entitlement
    // data on top of it would resurrect a stale plan for a moment, so just drop the result.
    AccountSnapshot s = getSnapshot();
    if (s.state != AccountState::SignedIn)
        return;

    s.plan = entitlement.plan;
    s.monthlyRequestLimit = entitlement.monthlyRequestLimit;
    s.requestsUsed = entitlement.requestsUsed;
    s.entitlementKnown = true;
    publishSnapshot(s);
}

void AccountService::completeSignIn(const juce::String& newAccessToken, const juce::String& newRefreshToken) {
    // Guards the window between "the network round trip that produced this token pair completed"
    // and "it's persisted and published": without this check, a cancelSignIn() or signOut() that
    // lands in exactly that window would be silently overwritten — the user cancels or signs out,
    // and completeSignIn() signs them (back) in anyway a moment later. threadShouldExit() covers
    // the same race during shutdown.
    if (threadShouldExit() || cancelRequested.load()) {
        AccountSnapshot s; // SignedOut, no error — same as any other cancelled-flow outcome
        publishSnapshot(s);
        juce::Logger::writeToLog("AccountService: sign-in discarded (cancelled).");
        return;
    }

    // Persist before use: the refresh token just rotated, and a crash between "use" and
    // "persist" here would leave a dead token in the keychain and silently sign the user out
    // next launch.
    if (!tokenStore->save(newRefreshToken)) {
        AccountSnapshot s;
        s.state = AccountState::SignedOut;
        s.lastError = "Error: could not persist the refresh token.";
        publishSnapshot(s);
        juce::Logger::writeToLog("AccountService: sign-in failed: could not persist the refresh token.");
        return;
    }

    juce::String email;
    const auto me = authClient.fetchMe(newAccessToken, cancelRequested);
    if (me.ok) {
        email = me.email;
    } else {
        // Cosmetic only: don't fail an otherwise-successful sign-in over a profile fetch.
        juce::Logger::writeToLog("AccountService: fetchMe after sign-in failed (non-fatal): " + me.transportError);
    }

    // Same non-fatal contract as fetchMe() above: entitlement is cosmetic (a usage indicator, an
    // upgrade path), never a reason to fail an otherwise-successful sign-in.
    juce::String plan;
    int monthlyRequestLimit = 0;
    int requestsUsed = 0;
    bool entitlementKnown = false;
    const auto entitlement = authClient.fetchEntitlement(newAccessToken, cancelRequested);
    if (entitlement.ok) {
        plan = entitlement.plan;
        monthlyRequestLimit = entitlement.monthlyRequestLimit;
        requestsUsed = entitlement.requestsUsed;
        entitlementKnown = true;
    } else {
        juce::Logger::writeToLog("AccountService: fetchEntitlement after sign-in failed (non-fatal): " +
                                 entitlement.transportError);
    }

    setAccessTokenFromWorker(newAccessToken);

    AccountSnapshot s;
    s.state = AccountState::SignedIn;
    s.email = email;
    s.plan = plan;
    s.monthlyRequestLimit = monthlyRequestLimit;
    s.requestsUsed = requestsUsed;
    s.entitlementKnown = entitlementKnown;
    publishSnapshot(s);

    juce::Logger::writeToLog("AccountService: sign-in succeeded (" + maskEmail(email) + ").");
}

void AccountService::publishSnapshot(const AccountSnapshot& newSnapshot) {
    {
        const juce::ScopedLock sl(stateLock);
        snapshot = newSnapshot;
    }

    if (onStateChanged) {
        auto callback = onStateChanged;
        juce::MessageManager::callAsync([callback] { callback(); });
    }
}

void AccountService::setAccessTokenFromWorker(const juce::String& token) {
    {
        const juce::ScopedLock sl(stateLock);
        accessToken = token;
    }

    if (onAccessTokenChanged) {
        auto callback = onAccessTokenChanged;
        juce::MessageManager::callAsync([callback, token] { callback(token); });
    }
}

} // namespace synth
