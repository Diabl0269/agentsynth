#include "AI/UpgradeUrl.h"
#include "AccountTestFixture.h"
#include "UI/Assistant/PlanBadge.h"
#include "UI/Settings/AccountSettingsTab.h"
#include <gtest/gtest.h>

using account_test::FakeAccountServer;
using account_test::makeSignedInService;
using account_test::realClick;
using synth::AccountFlowPanel;

namespace {
std::unique_ptr<synth::AccountService> makeSignedOutService(FakeAccountServer& server) {
    return std::make_unique<synth::AccountService>("http://mock-host:8787", server.performer(),
                                                   std::make_unique<synth::InMemoryTokenStore>());
}
} // namespace

TEST(AccountSettingsTabTest, SignedOutShowsTheSignInLineAndButtonOnly) {
    FakeAccountServer server;
    auto service = makeSignedOutService(server);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);

    EXPECT_EQ(tab.getSignedOutTextForTest(), juce::String("Sign in to see your plan and manage your account"));
    EXPECT_TRUE(tab.getSignInButtonForTest().isVisible());
    EXPECT_FALSE(tab.getSignOutButtonForTest().isVisible());
    EXPECT_FALSE(tab.getManageButtonForTest().isVisible());
    EXPECT_FALSE(tab.getDeleteButtonForTest().isVisible());
}

TEST(AccountSettingsTabTest, SignedInProShowsEmailPlanRenewalAndManage) {
    FakeAccountServer server;
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);

    EXPECT_EQ(tab.getEmailTextForTest(), juce::String("jane@example.com"));
    EXPECT_EQ(tab.getPlanTextForTest(), juce::String::fromUTF8("Pro \xc2\xb7 42 of 500 requests this month"));
    EXPECT_TRUE(tab.getPeriodTextForTest().startsWith("Renews "));
    EXPECT_EQ(tab.getManageButtonForTest().getButtonText(), juce::String("Manage subscription"));
    EXPECT_TRUE(tab.getSignOutButtonForTest().isVisible());
    EXPECT_TRUE(tab.getDeleteButtonForTest().isVisible());
    EXPECT_FALSE(tab.getSignInButtonForTest().isVisible());
}

TEST(AccountSettingsTabTest, ACancelledProSubscriptionShowsWhenItEnds) {
    FakeAccountServer server;
    server.cancelAtPeriodEnd = true;
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());

    EXPECT_TRUE(tab.getPeriodTextForTest().startsWith("Ends "));
}

TEST(AccountSettingsTabTest, FreeShowsItsUsageNoPeriodLineAndUpgradeToPro) {
    FakeAccountServer server;
    server.plan = "free";
    server.periodEnd = "";
    server.requestsUsed = 7;
    server.monthlyLimit = 25;
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);

    EXPECT_EQ(tab.getPlanTextForTest(), juce::String::fromUTF8("Free \xc2\xb7 7 of 25 requests this month"));
    EXPECT_TRUE(tab.getPeriodTextForTest().isEmpty());
    EXPECT_EQ(tab.getManageButtonForTest().getButtonText(), juce::String("Upgrade to Pro"));
}

TEST(AccountSettingsTabTest, UpgradeToProOpensTheCheckoutLinkWithTheEmailPrefilled) {
    FakeAccountServer server;
    server.plan = "free";
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);
    juce::URL opened;
    tab.setUrlOpenerForTesting([&](const juce::URL& url) { opened = url; });

    EXPECT_TRUE(realClick(tab.getManageButtonForTest()));

    EXPECT_EQ(opened.toString(true), synth::buildUpgradeUrl("jane@example.com").toString(true));
}

TEST(AccountSettingsTabTest, SignOutClearsTheSessionAndShowsTheSignedOutView) {
    FakeAccountServer server;
    synth::InMemoryTokenStore* store = nullptr;
    auto service = makeSignedInService(server, &store);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);

    EXPECT_TRUE(realClick(tab.getSignOutButtonForTest()));

    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedOut);
    EXPECT_TRUE(store->load().isEmpty());
    EXPECT_TRUE(tab.getSignInButtonForTest().isVisible());
    EXPECT_FALSE(tab.getSignOutButtonForTest().isVisible());
}

TEST(AccountSettingsTabTest, FollowsTheSnapshotWhenSigningInHappensElsewhere) {
    FakeAccountServer server;
    auto store = std::make_unique<synth::InMemoryTokenStore>();
    store->save("stored-refresh-token");
    synth::AccountService service("http://mock-host:8787", server.performer(), std::move(store));
    AccountSettingsTab tab(&service);
    tab.setSize(520, 400);
    ASSERT_TRUE(tab.getSignInButtonForTest().isVisible());

    service.attemptSilentSignIn();
    ASSERT_TRUE(account_test::waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    tab.syncFromSnapshot();

    EXPECT_TRUE(tab.getManageButtonForTest().isVisible());
    EXPECT_EQ(tab.getEmailTextForTest(), juce::String("jane@example.com"));
}

TEST(AccountSettingsTabTest, ManageAndDeleteButtonsAreNamedAndTooltipped) {
    FakeAccountServer server;
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());

    EXPECT_EQ(tab.getManageButtonForTest().getTooltip(),
              juce::String("Opens the billing portal, where you can change payment details, see invoices or cancel"));
    EXPECT_EQ(tab.getDeleteButtonForTest().getTitle(), juce::String("Delete account"));
    EXPECT_TRUE(tab.getDeleteButtonForTest().getTooltip().isNotEmpty());
    EXPECT_EQ(tab.getSignOutButtonForTest().getTitle(), juce::String("Sign out"));
    EXPECT_TRUE(tab.getSignOutButtonForTest().getTooltip().isNotEmpty());
    EXPECT_EQ(tab.getSignInButtonForTest().getTitle(), juce::String("Sign in"));
    EXPECT_TRUE(tab.getSignInButtonForTest().getTooltip().isNotEmpty());
}

TEST(AccountSettingsTabTest, TabReachesManageThenSignOutThenDelete) {
    FakeAccountServer server;
    auto service = makeSignedInService(server);
    AccountSettingsTab tab(service.get());
    tab.setSize(520, 400);

    juce::KeyboardFocusTraverser traverser;
    juce::Component* first = traverser.getDefaultComponent(&tab);
    ASSERT_NE(first, nullptr);
    EXPECT_EQ(first, &tab.getManageButtonForTest());
    auto* second = traverser.getNextComponent(first);
    EXPECT_EQ(second, &tab.getSignOutButtonForTest());
    EXPECT_EQ(traverser.getNextComponent(second), &tab.getDeleteButtonForTest());
}

namespace {
// A tab on `service` whose foreground state the test sets; the poll it runs is the one the timer runs.
struct ForegroundHarness {
    bool foreground = true;
    std::unique_ptr<AccountSettingsTab> tab;
    explicit ForegroundHarness(synth::AccountService* service) {
        tab = std::make_unique<AccountSettingsTab>(service);
        tab->setForegroundCheckForTesting([this] { return foreground; });
        tab->setVisible(true); // not on a desktop, so isShowing() stays false and no timer runs: poll by hand
        tab->pollForegroundForTest();
    }
};
} // namespace

TEST(AccountSettingsTabTest, ReturningToTheAppWhileSignedInRefreshesThePlanOnce) {
    FakeAccountServer server;
    auto service = makeSignedInService(server);
    ForegroundHarness h(service.get());
    const int before = server.count("GET", "/v1/entitlement");

    h.foreground = false;
    h.tab->pollForegroundForTest();
    h.foreground = true;
    h.tab->pollForegroundForTest();
    h.tab->pollForegroundForTest(); // still in the foreground: no second request

    EXPECT_TRUE(account_test::waitUntil([&] { return server.count("GET", "/v1/entitlement") == before + 1; }));
    account_test::waitUntil([] { return false; }, std::chrono::milliseconds{150});
    EXPECT_EQ(server.count("GET", "/v1/entitlement"), before + 1);
}

TEST(AccountSettingsTabTest, StayingInTheForegroundDoesNotRefreshThePlan) {
    FakeAccountServer server;
    auto service = makeSignedInService(server);
    ForegroundHarness h(service.get());
    const int before = server.count("GET", "/v1/entitlement");

    for (int i = 0; i < 3; ++i)
        h.tab->pollForegroundForTest();
    account_test::waitUntil([] { return false; }, std::chrono::milliseconds{150});

    EXPECT_EQ(server.count("GET", "/v1/entitlement"), before);
}

TEST(AccountSettingsTabTest, ReturningToTheAppSignedOutDoesNotRefreshThePlan) {
    FakeAccountServer server;
    auto service = makeSignedOutService(server);
    ForegroundHarness h(service.get());

    h.foreground = false;
    h.tab->pollForegroundForTest();
    h.foreground = true;
    h.tab->pollForegroundForTest();
    account_test::waitUntil([] { return false; }, std::chrono::milliseconds{150});

    EXPECT_EQ(server.count("GET", "/v1/entitlement"), 0);
}

TEST(PlanTextTest, FormatTextAndPeriodLine) {
    synth::AccountSnapshot snapshot;
    EXPECT_TRUE(synth::PlanBadge::formatText(snapshot).isEmpty());

    snapshot.state = synth::AccountState::SignedIn;
    snapshot.entitlementKnown = true;
    snapshot.plan = "pro";
    snapshot.requestsUsed = 1203;
    snapshot.monthlyRequestLimit = 10000;
    EXPECT_EQ(synth::PlanBadge::formatText(snapshot), juce::String::fromUTF8("Pro \xc2\xb7 1203 / 10000 this month"));
    EXPECT_EQ(synth::PlanBadge::formatText(snapshot, true),
              juce::String::fromUTF8("Pro \xc2\xb7 1203 of 10000 requests this month"));

    EXPECT_TRUE(synth::PlanBadge::formatPeriodLine(snapshot).isEmpty()); // no period end known
    snapshot.periodEndIso = "2026-08-18T12:00:00Z";
    EXPECT_TRUE(synth::PlanBadge::formatPeriodLine(snapshot).startsWith("Renews "));
    snapshot.cancelAtPeriodEnd = true;
    EXPECT_TRUE(synth::PlanBadge::formatPeriodLine(snapshot).startsWith("Ends "));
    snapshot.plan = "free";
    EXPECT_TRUE(synth::PlanBadge::formatPeriodLine(snapshot).isEmpty());
    snapshot.plan = "pro";
    snapshot.periodEndIso = "not a date";
    EXPECT_TRUE(synth::PlanBadge::formatPeriodLine(snapshot).isEmpty());
}
