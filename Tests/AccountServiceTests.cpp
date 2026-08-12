#include "../Source/AI/AccountService.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/Auth/TokenStore.h"
#include <atomic>
#include <chrono>
#include <functional>
#include <gtest/gtest.h>
#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <vector>

namespace {

using synth::AccountService;
using synth::AccountSnapshot;
using synth::AccountState;
using synth::AuthClient;
using synth::InMemoryTokenStore;

const juce::String kHost = "http://mock-host:8787";
constexpr std::chrono::milliseconds kTimeout{10000};

AuthClient::HttpResult makeStatus(int status, const juce::String& body) {
    AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    return result;
}

AuthClient::HttpResult makeTransportFailure() {
    AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = "offline";
    return result;
}

// Pumps the JUCE message loop (so MessageManager::callAsync-posted callbacks actually run) while
// polling `predicate`, up to `timeout`. Generalizes this suite's existing
// `runDispatchLoopUntil()`-from-the-test-thread idiom (see AIUndoTests.cpp,
// AIChatComponentTests.cpp) into a bounded poll, since AccountService's worker thread does real
// (if short) sleeps between device-code polls.
template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = kTimeout) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

// Routes AuthClient's requests to canned responses by URL suffix, and records enough about each
// call (count, per-endpoint arrival timestamps) for assertions that don't need the real network.
class FakeAuthServer {
public:
    AuthClient::HttpResult deviceCodeResponse = makeTransportFailure();
    AuthClient::HttpResult meResponse = makeStatus(200, R"({"id":"","email":"","display_name":"","created_at":""})");
    AuthClient::HttpResult revokeResponse = makeStatus(200, "");
    // Defaults to a transport failure, same as every other unset canned response here — tests that
    // don't care about entitlement get the same "fetchEntitlement failed, non-fatal" path
    // completeSignIn() already tolerates for fetchMe(), so they don't need to set this explicitly.
    AuthClient::HttpResult entitlementResponse = makeTransportFailure();
    // Consumed FIFO by /v1/auth/token calls (covers both the device-code poll and refreshToken());
    // the last entry repeats once exhausted, so a test that only cares about the first N calls
    // doesn't need to size this exactly.
    std::vector<AuthClient::HttpResult> tokenResponses;

    mutable std::mutex mutex;
    int deviceCodeCallCount = 0;
    int tokenCallCount = 0;
    int meCallCount = 0;
    int revokeCallCount = 0;
    int entitlementCallCount = 0;
    std::vector<std::chrono::steady_clock::time_point> tokenCallTimes;

    AuthClient::HttpPerformer performer() {
        return [this](const juce::String&, const juce::String& url, const juce::StringPairArray&, const juce::String&,
                      int, const std::atomic<bool>&) -> AuthClient::HttpResult {
            const std::lock_guard<std::mutex> lock(mutex);

            if (url.endsWith("/v1/auth/device/code")) {
                ++deviceCodeCallCount;
                return deviceCodeResponse;
            }
            if (url.endsWith("/v1/auth/token")) {
                tokenCallTimes.push_back(std::chrono::steady_clock::now());
                const int index = tokenCallCount++;
                if (beforeTokenResponse)
                    beforeTokenResponse();
                if (tokenResponses.empty())
                    return makeTransportFailure();
                return tokenResponses[static_cast<size_t>(
                    juce::jmin(index, static_cast<int>(tokenResponses.size()) - 1))];
            }
            if (url.endsWith("/v1/auth/me")) {
                ++meCallCount;
                return meResponse;
            }
            if (url.endsWith("/v1/auth/revoke")) {
                ++revokeCallCount;
                return revokeResponse;
            }
            if (url.endsWith("/v1/entitlement")) {
                ++entitlementCallCount;
                return entitlementResponse;
            }
            return makeTransportFailure();
        };
    }

    int entitlementCalls() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return entitlementCallCount;
    }

    int tokenCalls() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return tokenCallCount;
    }

    // Set by a test to run a hook (e.g. cancelSignIn()/signOut() on the AccountService under
    // test) at the exact moment a /v1/auth/token response is about to be handed back to
    // AccountService — the narrow window completeSignIn()'s cancellation guard exists to close.
    std::function<void()> beforeTokenResponse;
};

AuthClient::HttpResult makeDeviceCodeResponse(int interval = 0) {
    return makeStatus(200, "{"
                           "\"device_code\":\"dc1\","
                           "\"user_code\":\"ABCD-1234\","
                           "\"verification_uri\":\"https://example.com/activate\","
                           "\"verification_uri_complete\":\"https://example.com/activate?user_code=ABCD-1234\","
                           "\"expires_in\":900,"
                           "\"interval\":" +
                               juce::String(interval) + "}");
}

AuthClient::HttpResult makeTokenSuccess(const juce::String& accessToken, const juce::String& refreshToken) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("access_token", accessToken);
    obj->setProperty("token_type", "Bearer");
    obj->setProperty("expires_in", 3600);
    obj->setProperty("refresh_token", refreshToken);
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

AuthClient::HttpResult makeTokenError(const juce::String& error, const juce::String& description = {}) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("error", error);
    obj->setProperty("error_description", description);
    return makeStatus(400, juce::JSON::toString(juce::var(obj.get())));
}

AuthClient::HttpResult makeMeSuccess(const juce::String& email) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", "user-1");
    obj->setProperty("email", email);
    obj->setProperty("display_name", "Test User");
    obj->setProperty("created_at", "2024-01-01");
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

AuthClient::HttpResult makeEntitlementSuccess(const juce::String& plan, int limit, int used) {
    juce::DynamicObject::Ptr usage = new juce::DynamicObject();
    usage->setProperty("requests_used", used);
    usage->setProperty("period_start", "2026-08-01");

    juce::DynamicObject::Ptr limits = new juce::DynamicObject();
    limits->setProperty("monthly_requests", limit);

    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("plan", plan);
    obj->setProperty("status", "active");
    obj->setProperty("period_end", juce::var());
    obj->setProperty("cancel_at_period_end", false);
    obj->setProperty("limits", juce::var(limits.get()));
    obj->setProperty("usage", juce::var(usage.get()));
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

// A TokenStore that lets a test observe (and, via saveResult, fail) the exact moment save() is
// called — used to verify completeSignIn()'s ordering directly rather than inferring it from
// state observed well after the fact.
class SpyTokenStore : public synth::TokenStore {
public:
    std::function<void(const juce::String&)> onSave;
    bool saveResult = true;

    bool save(const juce::String& refreshToken) override {
        if (onSave)
            onSave(refreshToken);
        if (!saveResult)
            return false;
        token = refreshToken;
        return true;
    }

    juce::String load() const override { return token; }
    void clear() override { token.clear(); }

private:
    juce::String token;
};

} // namespace

// ============================================================================
// Device sign-in: full happy path
// ============================================================================

TEST(AccountServiceTest, FullHappyPathDeviceSignIn) {
    FakeAuthServer server;
    // A non-zero interval leaves a real (if short) window between requestDeviceCode() and the
    // first poll, during which the published SigningIn snapshot is observable below — with
    // interval=0 the whole flow can complete before the test ever gets to check for it, since
    // getSnapshot() returns only the latest state, not a stream of every transition.
    server.deviceCodeResponse = makeDeviceCodeResponse(/*interval=*/1);
    server.tokenResponses = {makeTokenError("authorization_pending"), makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("jane@example.com");

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    auto* tokenStorePtr = tokenStore.get();
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] {
        return service.getSnapshot().state == AccountState::SigningIn && service.getSnapshot().userCode.isNotEmpty();
    })) << "never reached SigningIn with a populated userCode";
    EXPECT_EQ(service.getSnapshot().userCode, juce::String("ABCD-1234"));
    EXPECT_EQ(service.getSnapshot().verificationUriComplete,
              juce::String("https://example.com/activate?user_code=ABCD-1234"));

    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }))
        << "never reached SignedIn";
    EXPECT_EQ(service.getSnapshot().email, juce::String("jane@example.com"));
    EXPECT_EQ(service.getAccessToken(), juce::String("at1"));
    EXPECT_EQ(tokenStorePtr->load(), juce::String("rt1"));
}

// ============================================================================
// slow_down
// ============================================================================

TEST(AccountServiceTest, SlowDownCausesSecondPollAfterBumpedInterval) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(/*interval=*/0);
    // Keeps returning authorization_pending after the bump so the flow doesn't complete out from
    // under the assertions below.
    server.tokenResponses = {makeTokenError("slow_down"), makeTokenError("authorization_pending")};

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return server.tokenCalls() >= 1; })) << "the slow_down poll never happened";

    // The interval was 0 before slow_down bumped it by 5s: a second poll arriving within the next
    // half second would mean the bump was never applied.
    juce::Thread::sleep(500);
    EXPECT_EQ(server.tokenCalls(), 1) << "the poll after slow_down must not fire before the bumped interval elapses";

    ASSERT_TRUE(waitUntil([&] { return server.tokenCalls() >= 2; }, std::chrono::milliseconds(9000)))
        << "the second poll never arrived even after waiting past the bumped interval";

    const std::lock_guard<std::mutex> lock(server.mutex);
    ASSERT_GE(server.tokenCallTimes.size(), 2u);
    const auto elapsedMs =
        std::chrono::duration_cast<std::chrono::milliseconds>(server.tokenCallTimes[1] - server.tokenCallTimes[0])
            .count();
    EXPECT_GE(elapsedMs, 4000) << "expected roughly a 5s bumped interval between the slow_down poll and the next";
}

// ============================================================================
// Terminal poll errors
// ============================================================================

TEST(AccountServiceTest, ExpiredTokenEndsSignedOutWithError) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenError("expired_token", "the device code expired")};

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] {
        return service.getSnapshot().state == AccountState::SignedOut && service.getSnapshot().lastError.isNotEmpty();
    }));
    EXPECT_EQ(service.getSnapshot().lastError, juce::String("the device code expired"));
}

TEST(AccountServiceTest, AccessDeniedEndsSignedOutWithError) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenError("access_denied", "user declined")};

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] {
        return service.getSnapshot().state == AccountState::SignedOut && service.getSnapshot().lastError.isNotEmpty();
    }));
    EXPECT_EQ(service.getSnapshot().lastError, juce::String("user declined"));
}

// ============================================================================
// Rotation-before-use
// ============================================================================

// Because onAccessTokenChanged is dispatched via MessageManager::callAsync, a test that only
// checks the TokenStore's contents once the callback eventually fires can't distinguish "saved
// before the access token was set" from "saved after" — both look identical by the time a pumped
// message loop gets around to running the callback. A SpyTokenStore that observes the exact
// moment save() runs (synchronously, on the worker thread, before either notification is even
// queued) is the only way to actually pin the ordering down.
TEST(AccountServiceTest, SaveHappensBeforeTheAccessTokenIsSetOrPublished) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("");

    auto spyStore = std::make_unique<SpyTokenStore>();
    auto* spyPtr = spyStore.get();
    AccountService service{kHost, server.performer(), std::move(spyStore)};

    bool saveObserved = false;
    juce::String tokenPassedToSave;
    bool accessTokenWasEmptyAtSaveTime = false;
    AccountState stateAtSaveTime = AccountState::SignedOut;
    spyPtr->onSave = [&](const juce::String& token) {
        saveObserved = true;
        tokenPassedToSave = token;
        accessTokenWasEmptyAtSaveTime = service.getAccessToken().isEmpty();
        stateAtSaveTime = service.getSnapshot().state;
    };

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    ASSERT_TRUE(saveObserved) << "save() was never called";
    EXPECT_EQ(tokenPassedToSave, juce::String("rt1"));
    EXPECT_TRUE(accessTokenWasEmptyAtSaveTime)
        << "the in-memory access token must not be set until after the refresh token is persisted";
    EXPECT_NE(stateAtSaveTime, AccountState::SignedIn)
        << "the SignedIn snapshot must not be published until after the refresh token is persisted";
}

TEST(AccountServiceTest, SaveFailureDuringSignInEndsSignedOutWithErrorAndNoAccessToken) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};

    auto spyStore = std::make_unique<SpyTokenStore>();
    spyStore->saveResult = false;
    AccountService service{kHost, server.performer(), std::move(spyStore)};

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] {
        return service.getSnapshot().state == AccountState::SignedOut && service.getSnapshot().lastError.isNotEmpty();
    })) << "a save() failure must not be treated as a successful sign-in";
    EXPECT_TRUE(service.getAccessToken().isEmpty());
}

// ============================================================================
// Cancellation / sign-out racing a completing sign-in
// ============================================================================

// Simulates a cancelSignIn() landing in the exact window between "the poll already returned
// success" and "completeSignIn() persists/publishes it" — the race completeSignIn()'s
// cancellation guard exists to close. Without that guard this test fails: the flow completes and
// signs the user in despite the cancel.
TEST(AccountServiceTest, CancelRacingASuccessfulPollDiscardsTheResultInsteadOfSigningIn) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    auto* tokenStorePtr = tokenStore.get();
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    server.beforeTokenResponse = [&] { service.cancelSignIn(); };

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return server.tokenCalls() >= 1; })) << "the poll never happened";
    // No further event to await: the discarded result produces no SignedIn transition to wait
    // for, so give completeSignIn()'s guard a moment to run to completion instead.
    juce::Thread::sleep(300);

    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_TRUE(service.getAccessToken().isEmpty());
    EXPECT_TRUE(tokenStorePtr->load().isEmpty()) << "a cancelled sign-in must not persist the rotated refresh token";
}

// Same race, but via signOut() instead of cancelSignIn() — signOut() must be the last word even
// when it lands mid-flight, or "sign out" can be silently undone by a sign-in that was already in
// flight.
TEST(AccountServiceTest, SignOutRacingAnInFlightSignInIsNotUndoneByIt) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    auto* tokenStorePtr = tokenStore.get();
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    server.beforeTokenResponse = [&] { service.signOut(); };

    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return server.tokenCalls() >= 1; })) << "the poll never happened";
    juce::Thread::sleep(300);

    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_TRUE(service.getAccessToken().isEmpty());
    EXPECT_TRUE(tokenStorePtr->load().isEmpty()) << "signOut() racing a completing sign-in must not be reversed by it";
}

// ============================================================================
// attemptSilentSignIn
// ============================================================================

TEST(AccountServiceTest, AttemptSilentSignInSuccessReachesSignedIn) {
    FakeAuthServer server;
    server.tokenResponses = {makeTokenSuccess("at2", "rt2")};
    server.meResponse = makeMeSuccess("a@b.com");

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    service.attemptSilentSignIn();

    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    EXPECT_EQ(service.getAccessToken(), juce::String("at2"));
    EXPECT_TRUE(service.getSnapshot().lastError.isEmpty());
}

TEST(AccountServiceTest, AttemptSilentSignInInvalidGrantClearsStoreAndStaysSignedOutSilently) {
    FakeAuthServer server;
    server.tokenResponses = {makeTokenError("invalid_grant", "refresh token revoked")};

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    auto* tokenStorePtr = tokenStore.get();
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    service.attemptSilentSignIn();

    ASSERT_TRUE(waitUntil([&] { return tokenStorePtr->load().isEmpty(); }))
        << "the dead refresh token was never cleared";
    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_TRUE(service.getSnapshot().lastError.isEmpty()) << "an invalid_grant on silent sign-in must stay silent";
}

TEST(AccountServiceTest, AttemptSilentSignInWithNoStoredTokenStaysSignedOutWithoutHittingNetwork) {
    FakeAuthServer server;

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.attemptSilentSignIn();

    // Give the (nonexistent) job a moment to prove itself absent.
    juce::Thread::sleep(200);
    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_EQ(server.tokenCalls(), 0);
}

// ============================================================================
// entitlement (P4-4)
// ============================================================================

TEST(AccountServiceTest, CompleteSignInPopulatesEntitlementFromSuccessfulFetch) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("jane@example.com");
    server.entitlementResponse = makeEntitlementSuccess("pro", 10000, 743);

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; })) << "entitlement was never populated";

    const auto snapshot = service.getSnapshot();
    EXPECT_EQ(snapshot.plan, juce::String("pro"));
    EXPECT_EQ(snapshot.monthlyRequestLimit, 10000);
    EXPECT_EQ(snapshot.requestsUsed, 743);
}

TEST(AccountServiceTest, CompleteSignInLeavesEntitlementAtDefaultsWhenFetchFails) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("jane@example.com");
    // entitlementResponse left at its default transport failure.

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.beginSignIn();

    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    // Sign-in must not have been blocked by the entitlement failure — give the (failed) fetch a
    // moment to have definitely run, then assert it left no trace.
    juce::Thread::sleep(200);

    const auto snapshot = service.getSnapshot();
    EXPECT_FALSE(snapshot.entitlementKnown);
    EXPECT_TRUE(snapshot.plan.isEmpty());
    EXPECT_EQ(snapshot.monthlyRequestLimit, 0);
    EXPECT_EQ(snapshot.requestsUsed, 0);
}

TEST(AccountServiceTest, RefreshEntitlementUpdatesSnapshotWithoutChangingSignInState) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("jane@example.com");
    server.entitlementResponse = makeEntitlementSuccess("free", 1000, 100);

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    service.beginSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    ASSERT_EQ(service.getSnapshot().plan, juce::String("free"));
    const int entitlementCallsAfterSignIn = server.entitlementCalls();

    // Simulate "the user upgraded mid-session": the server now reports Pro.
    server.entitlementResponse = makeEntitlementSuccess("pro", 10000, 101);
    service.refreshEntitlement();

    ASSERT_TRUE(waitUntil([&] { return server.entitlementCalls() > entitlementCallsAfterSignIn; }))
        << "refreshEntitlement() never hit the network";
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().plan == juce::String("pro"); }));

    const auto snapshot = service.getSnapshot();
    EXPECT_EQ(snapshot.state, AccountState::SignedIn) << "refreshEntitlement() must not touch sign-in state";
    EXPECT_EQ(snapshot.email, juce::String("jane@example.com")) << "refreshEntitlement() must not touch email";
    EXPECT_EQ(snapshot.monthlyRequestLimit, 10000);
    EXPECT_EQ(snapshot.requestsUsed, 101);
}

TEST(AccountServiceTest, RefreshEntitlementIsNoOpWhenSignedOut) {
    FakeAuthServer server;

    AccountService service{kHost, server.performer(), std::make_unique<InMemoryTokenStore>()};
    ASSERT_EQ(service.getSnapshot().state, AccountState::SignedOut);

    service.refreshEntitlement();

    juce::Thread::sleep(200);
    EXPECT_EQ(server.entitlementCalls(), 0);
}

// ============================================================================
// signOut
// ============================================================================

TEST(AccountServiceTest, SignOutSynchronouslyClearsStateAfterSuccessfulSignIn) {
    FakeAuthServer server;
    server.deviceCodeResponse = makeDeviceCodeResponse(0);
    server.tokenResponses = {makeTokenSuccess("at1", "rt1")};
    server.meResponse = makeMeSuccess("jane@example.com");

    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    auto* tokenStorePtr = tokenStore.get();
    AccountService service{kHost, server.performer(), std::move(tokenStore)};

    service.beginSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    ASSERT_TRUE(tokenStorePtr->load().isNotEmpty());

    service.signOut();

    // No waiting: signOut() must clear synchronously, on the calling thread.
    EXPECT_EQ(service.getSnapshot().state, AccountState::SignedOut);
    EXPECT_TRUE(service.getAccessToken().isEmpty());
    EXPECT_TRUE(tokenStorePtr->load().isEmpty());

    // The best-effort revoke still fires in the background afterward.
    ASSERT_TRUE(waitUntil([&] {
        const std::lock_guard<std::mutex> lock(server.mutex);
        return server.revokeCallCount >= 1;
    }));
}

// ============================================================================
// InMemoryTokenStore
// ============================================================================

TEST(InMemoryTokenStoreTest, RoundTripsSaveLoadClear) {
    InMemoryTokenStore store;
    EXPECT_TRUE(store.load().isEmpty());
    EXPECT_TRUE(store.save("abc-refresh-token"));
    EXPECT_EQ(store.load(), juce::String("abc-refresh-token"));
    store.clear();
    EXPECT_TRUE(store.load().isEmpty());
}
