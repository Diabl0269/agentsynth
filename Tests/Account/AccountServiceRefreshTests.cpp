// Bug kind session-expiry (docs: docs/ai/accounts.md#expiry-and-refresh): a session that works right after sign-in must
// keep working after the access token's hour is up. The guard for the kind: a fake auth server that issues short-lived
// tokens and a test clock, then the next call must refresh once (single-flight), two parallel calls must share that one
// refresh, and a session the server ended must say "sign in again" instead of failing with a raw 401.
#include "../AI/RemoteProvider/RemoteProviderTestFixture.h"
#include "AI/AccountService.h"
#include "Auth/InMemoryTokenStore.h"
#include <atomic>
#include <chrono>
#include <gtest/gtest.h>
#include <juce_events/juce_events.h>
#include <thread>
#include <vector>

namespace {

using synth::AccountService;
using synth::AccountState;
using synth::AuthClient;
using synth::InMemoryTokenStore;
using Outcome = synth::AccountService::RefreshOutcome;

const juce::String kAuthHost = "http://mock-host:8787";

AuthClient::HttpResult authStatus(int status, const juce::String& body) {
    AuthClient::HttpResult r;
    r.httpStatus = status;
    r.body = body;
    return r;
}

AuthClient::HttpResult tokenOk(const juce::String& access, const juce::String& refresh, int expiresIn = 3600) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("access_token", access);
    o->setProperty("token_type", "Bearer");
    o->setProperty("expires_in", expiresIn);
    o->setProperty("refresh_token", refresh);
    return authStatus(200, juce::JSON::toString(juce::var(o.get())));
}

AuthClient::HttpResult tokenError(const juce::String& code) {
    juce::DynamicObject::Ptr o = new juce::DynamicObject();
    o->setProperty("error", code);
    return authStatus(400, juce::JSON::toString(juce::var(o.get())));
}

// Issues a numbered access/refresh pair per /v1/auth/token call (at1/rt1, at2/rt2, ...) the way the real service does,
// and records every refresh token it was handed so reuse of a consumed one is visible.
struct FakeTokenServer {
    std::mutex mutex;
    int tokenCalls = 0;
    int delayMs = 0;       // makes parallel callers overlap
    juce::String failWith; // a token error code to answer with, or empty
    bool offline = false;
    std::vector<juce::String> redeemed; // refresh tokens presented, in order

    AuthClient::HttpPerformer performer() {
        return [this](const juce::String&, const juce::String& url, const juce::StringPairArray&,
                      const juce::String& body, int, const std::atomic<bool>&) -> AuthClient::HttpResult {
            if (url.endsWith("/v1/auth/token")) {
                if (delayMs > 0)
                    std::this_thread::sleep_for(std::chrono::milliseconds(delayMs));
                const std::lock_guard<std::mutex> lock(mutex);
                const int n = ++tokenCalls;
                redeemed.push_back(body);
                if (offline) {
                    AuthClient::HttpResult r;
                    r.transportFailed = true;
                    return r;
                }
                if (failWith.isNotEmpty())
                    return tokenError(failWith);
                return tokenOk("at" + juce::String(n), "rt" + juce::String(n));
            }
            if (url.endsWith("/v1/auth/me"))
                return authStatus(200, R"({"id":"u","email":"a@b.com","display_name":"A","created_at":"x"})");
            return authStatus(404, "{}");
        };
    }

    int calls() {
        const std::lock_guard<std::mutex> lock(mutex);
        return tokenCalls;
    }
};

template <typename Predicate>
bool pumpUntil(Predicate predicate) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

// A signed-in service on a fake clock: one silent sign-in has issued at1/rt1 (expires in an hour).
struct SignedInFixture {
    FakeTokenServer server;
    InMemoryTokenStore* store = nullptr;
    std::atomic<juce::int64> now{1'000'000'000};
    std::unique_ptr<AccountService> service;

    SignedInFixture() {
        auto tokenStore = std::make_unique<InMemoryTokenStore>();
        tokenStore->save("rt0");
        store = tokenStore.get();
        service = std::make_unique<AccountService>(kAuthHost, server.performer(), std::move(tokenStore));
        service->setClockForTesting([this] { return now.load(); });
        service->attemptSilentSignIn();
        EXPECT_TRUE(pumpUntil([&] { return service->getSnapshot().state == AccountState::SignedIn; }));
        EXPECT_EQ(service->getAccessToken(), juce::String("at1"));
    }
};

} // namespace

TEST(SessionExpiryTest, TimerRefreshesTheTokenFiveMinutesBeforeItExpires) {
    SignedInFixture f;
    f.now += 54 * 60 * 1000; // 6 minutes left: not due yet
    EXPECT_EQ(f.service->ensureFreshToken(), juce::String("at1")) << "refreshed before the five-minute lead";
    EXPECT_EQ(f.server.calls(), 1);

    f.now += 2 * 60 * 1000; // 4 minutes left: due
    f.service->refreshIfDue();
    ASSERT_TRUE(pumpUntil([&] { return f.service->getAccessToken() == "at2"; })) << "the due token was never refreshed";
    EXPECT_EQ(f.store->load(), juce::String("rt2")) << "the rotated refresh token must be stored";
    EXPECT_EQ(f.server.calls(), 2);
    EXPECT_EQ(f.service->getSnapshot().state, AccountState::SignedIn);

    // The new token has a full hour again: nothing more to do.
    EXPECT_EQ(f.service->ensureFreshToken(), juce::String("at2"));
    EXPECT_EQ(f.server.calls(), 2);
}

TEST(SessionExpiryTest, EnsureFreshTokenRefreshesAnExpiredTokenBeforeUse) {
    SignedInFixture f;
    f.now += 2 * 60 * 60 * 1000; // the app slept for two hours
    EXPECT_EQ(f.service->ensureFreshToken(), juce::String("at2"));
    EXPECT_EQ(f.server.calls(), 2);
    EXPECT_EQ(f.service->ensureFreshToken(), juce::String("at2")) << "a fresh token is returned without a second call";
    EXPECT_EQ(f.server.calls(), 2);
}

TEST(SessionExpiryTest, RefreshPersistsTheRotatedTokenBeforeTheNewAccessTokenIsExposed) {
    juce::String accessTokenWhenSaved;
    // Replace the store behaviour with a probe on the same instance: load/save through the real one.
    struct ProbeStore : synth::TokenStore {
        InMemoryTokenStore* inner;
        AccountService** svc;
        juce::String* seen;
        bool save(const juce::String& t) override {
            *seen = (*svc)->getAccessToken();
            return inner->save(t);
        }
        juce::String load() const override { return inner->load(); }
        void clear() override { inner->clear(); }
    };
    FakeTokenServer server;
    auto inner = std::make_unique<InMemoryTokenStore>();
    inner->save("rt0");
    auto probe = std::make_unique<ProbeStore>();
    AccountService* svcPtr = nullptr;
    probe->inner = inner.get();
    probe->svc = &svcPtr;
    probe->seen = &accessTokenWhenSaved;
    AccountService service{kAuthHost, server.performer(), std::move(probe)};
    svcPtr = &service;
    service.attemptSilentSignIn();
    ASSERT_TRUE(pumpUntil([&] { return service.getSnapshot().state == AccountState::SignedIn; }));
    ASSERT_EQ(service.getAccessToken(), juce::String("at1"));

    const auto result = service.refreshAccessToken("at1");
    EXPECT_EQ(result.outcome, Outcome::Refreshed);
    EXPECT_EQ(accessTokenWhenSaved, juce::String("at1")) << "the rotated refresh token was saved after at2 went live";
    EXPECT_EQ(service.getAccessToken(), juce::String("at2"));
}

TEST(SessionExpiryTest, AnExpiredTokenIsRefreshedOnceOnA401) {
    SignedInFixture f;
    const auto result = f.service->refreshAccessToken("at1");
    EXPECT_EQ(result.outcome, Outcome::Refreshed);
    EXPECT_EQ(result.accessToken, juce::String("at2"));
    EXPECT_EQ(f.server.calls(), 2);
    EXPECT_EQ(f.store->load(), juce::String("rt2"));
}

TEST(SessionExpiryTest, TwoParallel401sCauseOneRefreshCall) {
    SignedInFixture f;
    f.server.delayMs = 150; // the first refresh is still in flight when the second caller arrives
    AccountService::RefreshResult a, b;
    std::thread t1([&] { a = f.service->refreshAccessToken("at1"); });
    std::thread t2([&] { b = f.service->refreshAccessToken("at1"); });
    t1.join();
    t2.join();

    EXPECT_EQ(a.outcome, Outcome::Refreshed);
    EXPECT_EQ(b.outcome, Outcome::Refreshed);
    EXPECT_EQ(a.accessToken, juce::String("at2"));
    EXPECT_EQ(b.accessToken, juce::String("at2"));
    EXPECT_EQ(f.server.calls(), 2) << "sign-in plus exactly one refresh: a second redemption would revoke the family";
    int rt1Uses = 0;
    for (const auto& body : f.server.redeemed)
        if (body.contains("rt1"))
            ++rt1Uses;
    EXPECT_EQ(rt1Uses, 1) << "the consumed refresh token must be presented once";
}

TEST(SessionExpiryTest, AnEndedSessionLeavesSignedOutWithASignInAgainMessage) {
    SignedInFixture f;
    f.server.failWith = "invalid_grant";
    const auto result = f.service->refreshAccessToken("at1");
    EXPECT_EQ(result.outcome, Outcome::SessionEnded);
    EXPECT_TRUE(f.service->getAccessToken().isEmpty());
    EXPECT_TRUE(f.store->load().isEmpty()) << "the dead refresh token is cleared";
    const auto snap = f.service->getSnapshot();
    EXPECT_EQ(snap.state, AccountState::SignedOut);
    EXPECT_TRUE(snap.lastError.containsIgnoreCase("sign in again")) << snap.lastError;
}

TEST(SessionExpiryTest, OfflineRefreshKeepsTheSession) {
    SignedInFixture f;
    f.server.offline = true;
    const auto result = f.service->refreshAccessToken("at1");
    EXPECT_EQ(result.outcome, Outcome::Unavailable);
    EXPECT_EQ(f.service->getAccessToken(), juce::String("at1"));
    EXPECT_EQ(f.service->getSnapshot().state, AccountState::SignedIn);
    EXPECT_EQ(f.store->load(), juce::String("rt1")) << "the stored token is untouched when nothing was redeemed";
}

// The provider half: a request answered 401 asks for a new token and is sent once more with it.
TEST(SessionExpiryTest, ARequestAnswered401IsRetriedOnceWithTheRefreshedToken) {
    std::vector<juce::String> bearers;
    auto performer = [&](const juce::String&, const juce::StringPairArray& headers, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        bearers.push_back(headers.getValue("Authorization", ""));
        if (bearers.size() == 1)
            return makeStatus(401, R"({"error":"expired"})");
        return makeSuccess(R"({"data":{}})");
    };
    synth::RemoteProvider provider{kOfficialHost, performer};
    provider.setTestMode(true);
    provider.setAuthToken("old");
    int refreshes = 0;
    provider.setAuthRefresher([&](const juce::String& rejected) {
        ++refreshes;
        EXPECT_EQ(rejected, juce::String("old"));
        return juce::String("new");
    });

    MockPromptCallback callback;
    provider.sendPrompt({{"user", "hi"}}, [&](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    const auto response = callback.getResult();
    provider.stopThread(5000);

    EXPECT_TRUE(response.success);
    EXPECT_EQ(refreshes, 1);
    ASSERT_EQ(bearers.size(), 2u);
    EXPECT_EQ(bearers[0], juce::String("Bearer old"));
    EXPECT_EQ(bearers[1], juce::String("Bearer new"));
}

TEST(SessionExpiryTest, ASessionTheServerEndedShowsASignInPromptNotARaw401) {
    int sent = 0;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        ++sent;
        return makeStatus(401, "{}");
    };
    synth::RemoteProvider provider{kOfficialHost, performer};
    provider.setTestMode(true);
    provider.setAuthToken("old");
    provider.setAuthRefresher([](const juce::String&) { return juce::String(); }); // session ended

    MockPromptCallback callback;
    provider.sendPrompt({{"user", "hi"}}, [&](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    const auto response = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(response.success);
    EXPECT_EQ(response.error.kind, synth::AIProvider::AIErrorKind::Auth);
    EXPECT_TRUE(response.error.message.containsIgnoreCase("sign in again")) << response.error.message;
    EXPECT_FALSE(response.error.message.contains("401"));
    EXPECT_EQ(sent, 1) << "no retry without a new token";
}

TEST(SessionExpiryTest, ARetryThatIsRefusedAgainIsNotRetriedAgain) {
    int sent = 0;
    auto performer = [&](const juce::String&, const juce::StringPairArray&, const juce::String&, int,
                         const std::atomic<bool>&) -> synth::RemoteProvider::HttpResult {
        ++sent;
        return makeStatus(401, "{}");
    };
    synth::RemoteProvider provider{kOfficialHost, performer};
    provider.setTestMode(true);
    provider.setAuthToken("old");
    provider.setAuthRefresher([](const juce::String&) { return juce::String("new"); });

    MockPromptCallback callback;
    provider.sendPrompt({{"user", "hi"}}, [&](const synth::AIProvider::AIResponse& r) { callback(r); }, makeSchema());
    const auto response = callback.getResult();
    provider.stopThread(5000);

    EXPECT_FALSE(response.success);
    EXPECT_EQ(sent, 2) << "one send plus one retry, never a loop";
}
