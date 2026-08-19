#include "../Source/AI/AccountService.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/UI/PlanBadge.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

namespace {

using synth::AccountService;
using synth::AuthClient;
using synth::InMemoryTokenStore;
using synth::PlanBadge;

const juce::String kHost = "http://mock-host:8787";

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

// Routes AuthClient's requests by URL suffix — same idiom as AccountServiceTests.cpp's
// FakeAuthServer, trimmed to just what completeSignIn() needs (token/me/entitlement).
AuthClient::HttpPerformer makeSignInPerformer(const AuthClient::HttpResult& tokenResponse,
                                              const AuthClient::HttpResult& meResponse,
                                              const AuthClient::HttpResult& entitlementResponse) {
    return [=](const juce::String&, const juce::String& url, const juce::StringPairArray&, const juce::String&, int,
               const std::atomic<bool>&) -> AuthClient::HttpResult {
        if (url.endsWith("/v1/auth/token"))
            return tokenResponse;
        if (url.endsWith("/v1/auth/me"))
            return meResponse;
        if (url.endsWith("/v1/entitlement"))
            return entitlementResponse;
        return makeTransportFailure();
    };
}

AuthClient::HttpResult makeTokenSuccess(const juce::String& accessToken, const juce::String& refreshToken) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("access_token", accessToken);
    obj->setProperty("token_type", "Bearer");
    obj->setProperty("expires_in", 3600);
    obj->setProperty("refresh_token", refreshToken);
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds{10000}) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

} // namespace

TEST(PlanBadgeTest, ZeroHeightAndInvisibleWithNoAccountService) {
    PlanBadge badge;
    EXPECT_EQ(badge.getPreferredHeight(), 0);
    EXPECT_FALSE(badge.isVisible());
}

TEST(PlanBadgeTest, ZeroHeightWhenSignedOut) {
    AccountService service{kHost,
                           [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                              const juce::String&, int,
                              const std::atomic<bool>&) -> AuthClient::HttpResult { return makeTransportFailure(); },
                           std::make_unique<InMemoryTokenStore>()};

    PlanBadge badge;
    badge.setAccountService(&service);

    EXPECT_EQ(badge.getPreferredHeight(), 0);
    EXPECT_FALSE(badge.isVisible());

    badge.setAccountService(nullptr);
}

TEST(PlanBadgeTest, RendersFreePlanTextOnceEntitlementIsKnown) {
    auto performer = makeSignInPerformer(makeTokenSuccess("at1", "rt1"), makeMeSuccess("jane@example.com"),
                                         makeEntitlementSuccess("free", 1000, 240));
    // attemptSilentSignIn() (refreshToken()) rather than beginSignIn() (the device-code flow) —
    // this test only needs to reach completeSignIn(), and refreshToken() hits the same
    // /v1/auth/token endpoint makeSignInPerformer already answers, with no device-code step.
    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    AccountService service{kHost, performer, std::move(tokenStore)};

    PlanBadge badge;
    badge.setAccountService(&service);

    service.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    badge.refresh();

    EXPECT_TRUE(badge.isVisible());
    EXPECT_GT(badge.getPreferredHeight(), 0);

    juce::Label* label = nullptr;
    for (auto* child : badge.getChildren()) {
        if (auto* l = dynamic_cast<juce::Label*>(child))
            label = l;
    }
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->getText().contains("Free"));
    EXPECT_TRUE(label->getText().contains("240"));
    EXPECT_TRUE(label->getText().contains("1000"));

    badge.setAccountService(nullptr);
}

// ============================================================================
// isProPlan() — free function next to AccountSnapshot (Source/AI/AccountService.h)
// ============================================================================

TEST(IsProPlanTest, TrueForExactLowercasePro) {
    synth::AccountSnapshot snapshot;
    snapshot.entitlementKnown = true;
    snapshot.plan = "pro";
    EXPECT_TRUE(synth::isProPlan(snapshot));
}

TEST(IsProPlanTest, TrueForProRegardlessOfCase) {
    synth::AccountSnapshot snapshot;
    snapshot.entitlementKnown = true;
    snapshot.plan = "PrO";
    EXPECT_TRUE(synth::isProPlan(snapshot));
}

TEST(IsProPlanTest, FalseForFree) {
    synth::AccountSnapshot snapshot;
    snapshot.entitlementKnown = true;
    snapshot.plan = "free";
    EXPECT_FALSE(synth::isProPlan(snapshot));
}

TEST(IsProPlanTest, FalseForEmptyPlanString) {
    synth::AccountSnapshot snapshot;
    snapshot.entitlementKnown = true;
    snapshot.plan = "";
    EXPECT_FALSE(synth::isProPlan(snapshot));
}

// entitlementKnown == false: same as PlanBadgeTest.ZeroHeightWhenSignedOut's premise — a
// snapshot whose entitlement hasn't been fetched yet leaves `plan` at its default (empty), so
// isProPlan() answers false without needing to consult entitlementKnown itself.
TEST(IsProPlanTest, FalseWhenEntitlementNotYetKnown) {
    synth::AccountSnapshot snapshot; // default: entitlementKnown = false, plan = ""
    EXPECT_FALSE(snapshot.entitlementKnown);
    EXPECT_FALSE(synth::isProPlan(snapshot));
}

TEST(PlanBadgeTest, RendersProPlanTextOnceEntitlementIsKnown) {
    auto performer = makeSignInPerformer(makeTokenSuccess("at1", "rt1"), makeMeSuccess("jane@example.com"),
                                         makeEntitlementSuccess("pro", 10000, 1203));
    auto tokenStore = std::make_unique<InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    AccountService service{kHost, performer, std::move(tokenStore)};

    PlanBadge badge;
    badge.setAccountService(&service);

    service.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    badge.refresh();

    juce::Label* label = nullptr;
    for (auto* child : badge.getChildren()) {
        if (auto* l = dynamic_cast<juce::Label*>(child))
            label = l;
    }
    ASSERT_NE(label, nullptr);
    EXPECT_TRUE(label->getText().contains("Pro"));
    EXPECT_TRUE(label->getText().contains("1203"));
    EXPECT_TRUE(label->getText().contains("10000"));

    badge.setAccountService(nullptr);
}
