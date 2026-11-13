// The Manage subscription and Delete account popovers behind the Account tab, driven against a fake server.
#include "AccountTestFixture.h"
#include "Branding.h"
#include "UI/Settings/AccountSettingsTab.h"
#include <gtest/gtest.h>

using account_test::FakeAccountServer;
using account_test::makeSignedInService;
using account_test::realClick;
using synth::AccountFlowPanel;

namespace {

class AccountFlowTest : public ::testing::Test {
protected:
    void build(const char* plan = "pro") {
        server.plan = plan;
        service = makeSignedInService(server, &store);
        tab = std::make_unique<AccountSettingsTab>(service.get());
        tab->setSize(520, 400);
        tab->setHttpPerformerForTesting(server.performer());
        tab->setRunRequestsInlineForTesting(true); // worker and callback on this thread
        tab->setUrlOpenerForTesting([this](const juce::URL& url) { opened.push_back(url.toString(true)); });
    }

    std::unique_ptr<AccountFlowPanel> panel(AccountFlowPanel::Flow flow) {
        auto* opener =
            flow == AccountFlowPanel::Flow::manage ? &tab->getManageButtonForTest() : &tab->getDeleteButtonForTest();
        return tab->createFlowPanelForTest(flow, *opener);
    }

    static void tick(synth::LeavingSurveyPanel& survey, int index) {
        survey.getReasonToggleForTest(index).setToggleState(true, juce::dontSendNotification);
    }

    FakeAccountServer server;
    synth::InMemoryTokenStore* store = nullptr;
    std::unique_ptr<synth::AccountService> service;
    std::unique_ptr<AccountSettingsTab> tab;
    std::vector<juce::String> opened;
};

const juce::String kPortal = synth::branding::kBillingPortalUrl;

} // namespace

// ---- Manage subscription -----------------------------------------------------------------------

TEST_F(AccountFlowTest, ManageOffersTheTwoChoices) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::manageChoice);
    EXPECT_NE(p->findButtonForTest("Change payment or see invoices"), nullptr);
    EXPECT_NE(p->findButtonForTest("Cancel my subscription"), nullptr);
}

TEST_F(AccountFlowTest, ChangePaymentOpensThePortalAtOnce) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Change payment or see invoices")));

    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], kPortal);
    EXPECT_TRUE(p->wasCloseRequested());
    EXPECT_EQ(server.count("POST", "/v1/exit-survey"), 0);
}

TEST_F(AccountFlowTest, CancelShowsTheLeavingQuestionBeforeOpeningThePortal) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel my subscription")));

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::cancelSurvey);
    ASSERT_NE(p->getSurveyForTest(), nullptr);
    EXPECT_EQ(p->getSurveyForTest()->getKind(), synth::LeavingSurveyPanel::Kind::cancel);
    EXPECT_TRUE(opened.empty());
}

TEST_F(AccountFlowTest, CancelThenSkipOpensThePortalAndPostsNothing) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel my subscription")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getSkipButtonForTest()));

    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], kPortal);
    EXPECT_EQ(server.count("POST", "/v1/exit-survey"), 0);
}

TEST_F(AccountFlowTest, CancelThenContinuePostsTheAnswerAndOpensThePortal) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel my subscription")));
    tick(*p->getSurveyForTest(), 0);
    tick(*p->getSurveyForTest(), 3);
    p->getSurveyForTest()->getCommentEditorForTest().setText("Great, just not for me", false);
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getContinueButtonForTest()));

    ASSERT_EQ(opened.size(), 1u);
    EXPECT_EQ(opened[0], kPortal);
    const auto bodyVar = juce::JSON::parse(server.bodyOf("POST", "/v1/exit-survey"));
    auto* body = bodyVar.getDynamicObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->getProperty("kind").toString(), juce::String("cancel"));
    ASSERT_NE(body->getProperty("reasons").getArray(), nullptr);
    EXPECT_EQ(body->getProperty("reasons").getArray()->size(), 2);
    EXPECT_EQ(body->getProperty("comment").toString(), juce::String("Great, just not for me"));
}

TEST_F(AccountFlowTest, ContinueOpensThePortalEvenWhenThePostFails) {
    build();
    server.surveyStatus = 500;
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel my subscription")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getContinueButtonForTest()));

    EXPECT_EQ(opened.size(), 1u);
}

TEST_F(AccountFlowTest, ManageButtonOnTheTabOpensTheManagePanelForPro) {
    build();
    std::unique_ptr<AccountFlowPanel> launched;
    tab->setFlowLauncherForTesting([&](std::unique_ptr<AccountFlowPanel> p) { launched = std::move(p); });

    EXPECT_TRUE(realClick(tab->getManageButtonForTest()));

    ASSERT_NE(launched, nullptr);
    EXPECT_EQ(launched->getPage(), AccountFlowPanel::Page::manageChoice);
    EXPECT_TRUE(opened.empty()); // a Pro click opens the panel, never the browser directly
}

// ---- Delete account ----------------------------------------------------------------------------

TEST_F(AccountFlowTest, DeleteExplainsWhatIsRemovedAndOffersCancelAndDelete) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::deleteConfirm);
    EXPECT_EQ(p->getBodyTextForTest(),
              juce::String("Your account, sign-ins on every device, conversation history and shared prompt samples "
                           "are deleted right away. Invoices stay with our payment provider."));
    ASSERT_NE(p->findButtonForTest("Cancel"), nullptr);
    ASSERT_NE(p->findButtonForTest("Delete account"), nullptr);

    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel")));
    EXPECT_TRUE(p->wasCloseRequested());
    EXPECT_EQ(server.count("DELETE", "/v1/account"), 0);
}

TEST_F(AccountFlowTest, DeleteAsksTheLeavingQuestionBeforeAnyRequest) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::deleteSurvey);
    EXPECT_EQ(p->getSurveyForTest()->getKind(), synth::LeavingSurveyPanel::Kind::deleteAccount);
    EXPECT_EQ(server.count("DELETE", "/v1/account"), 0);
    EXPECT_EQ(server.count("POST", "/v1/exit-survey"), 0);
}

TEST_F(AccountFlowTest, SkipThenA204SignsOutClearsTokensAndShowsTheDeletedMessage) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getSkipButtonForTest()));

    EXPECT_EQ(server.count("POST", "/v1/exit-survey"), 0);
    EXPECT_EQ(server.count("DELETE", "/v1/account"), 1);
    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedOut);
    EXPECT_TRUE(store->load().isEmpty());
    EXPECT_TRUE(p->wasCloseRequested());
    EXPECT_EQ(tab->getNoticeTextForTest(), juce::String("Your account was deleted."));
    EXPECT_TRUE(tab->getSignInButtonForTest().isVisible());
}

TEST_F(AccountFlowTest, ContinueSendsTheSurveyBeforeTheDeleteRequest) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    tick(*p->getSurveyForTest(), 6);
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getContinueButtonForTest()));

    const int survey = server.indexOf("POST", "/v1/exit-survey");
    const int del = server.indexOf("DELETE", "/v1/account");
    ASSERT_GE(survey, 0);
    ASSERT_GE(del, 0);
    EXPECT_LT(survey, del);
    const auto bodyVar = juce::JSON::parse(server.bodyOf("POST", "/v1/exit-survey"));
    auto* body = bodyVar.getDynamicObject();
    ASSERT_NE(body, nullptr);
    EXPECT_EQ(body->getProperty("kind").toString(), juce::String("delete"));
    EXPECT_FALSE(body->hasProperty("comment"));
}

TEST_F(AccountFlowTest, DeleteStillRunsWhenTheSurveyPostFails) {
    build();
    server.surveyStatus = 500;
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getContinueButtonForTest()));

    EXPECT_EQ(server.count("DELETE", "/v1/account"), 1);
    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedOut);
}

TEST_F(AccountFlowTest, A409SubscriptionActiveTellsThemToCancelFirstAndOffersManage) {
    build();
    server.deleteStatus = 409;
    server.deleteBody = R"({"error":{"code":"SUBSCRIPTION_ACTIVE","message":"x"}})";
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getSkipButtonForTest()));

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::deleteFailed);
    EXPECT_EQ(p->getBodyTextForTest(), juce::String("Cancel your subscription first, then delete your account."));
    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedIn);
    ASSERT_NE(p->findButtonForTest("Manage subscription"), nullptr);
    EXPECT_TRUE(p->findButtonForTest("Manage subscription")->getTooltip().startsWith("Opens the billing portal"));

    ASSERT_TRUE(realClick(*p->findButtonForTest("Manage subscription")));
    EXPECT_EQ(p->getPage(), AccountFlowPanel::Page::manageChoice);
}

TEST_F(AccountFlowTest, AnotherServerErrorShowsItsMessageAndTryAgainRetries) {
    build();
    server.deleteStatus = 500;
    server.deleteBody = R"({"error":{"code":"INTERNAL","message":"Something went wrong on our side."}})";
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getSkipButtonForTest()));

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::deleteFailed);
    EXPECT_EQ(p->getBodyTextForTest(), juce::String("Something went wrong on our side."));
    ASSERT_NE(p->findButtonForTest("Try again"), nullptr);

    server.deleteStatus = 204;
    ASSERT_TRUE(realClick(*p->findButtonForTest("Try again")));
    EXPECT_EQ(server.count("DELETE", "/v1/account"), 2);
    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedOut);
}

TEST_F(AccountFlowTest, ANetworkFailureShowsANetworkLineAndTryAgain) {
    build();
    server.deleteTransportDown = true;
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Delete account")));
    ASSERT_TRUE(realClick(p->getSurveyForTest()->getSkipButtonForTest()));

    ASSERT_EQ(p->getPage(), AccountFlowPanel::Page::deleteFailed);
    EXPECT_EQ(p->getBodyTextForTest(), juce::String("Couldn't reach the server. Check your connection and try again."));
    EXPECT_NE(p->findButtonForTest("Try again"), nullptr);
    EXPECT_EQ(service->getSnapshot().state, synth::AccountState::SignedIn);
}

TEST_F(AccountFlowTest, DeleteButtonOnTheTabOpensTheConfirmationAndDeletesNothing) {
    build();
    std::unique_ptr<AccountFlowPanel> launched;
    tab->setFlowLauncherForTesting([&](std::unique_ptr<AccountFlowPanel> p) { launched = std::move(p); });

    EXPECT_TRUE(realClick(tab->getDeleteButtonForTest()));

    ASSERT_NE(launched, nullptr);
    EXPECT_EQ(launched->getPage(), AccountFlowPanel::Page::deleteConfirm);
    EXPECT_EQ(server.count("DELETE", "/v1/account"), 0);
}

// ---- Keyboard and accessibility ----------------------------------------------------------------

TEST_F(AccountFlowTest, EscapeClosesThePanel) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    EXPECT_TRUE(p->keyPressed(juce::KeyPress(juce::KeyPress::escapeKey)));
    EXPECT_TRUE(p->wasCloseRequested());
}

TEST_F(AccountFlowTest, EscapeFromTheCommentFieldReachesThePanel) {
    build();
    auto p = panel(AccountFlowPanel::Flow::manage);
    ASSERT_TRUE(realClick(*p->findButtonForTest("Cancel my subscription")));
    p->getSurveyForTest()->getCommentEditorForTest().onEscapeKey();
    EXPECT_TRUE(p->wasCloseRequested());
}

TEST_F(AccountFlowTest, EveryButtonOnEveryPageHasANameAndATooltip) {
    build();
    auto check = [](AccountFlowPanel& p) {
        for (auto* child : p.getChildren())
            for (auto* grand : child->getChildren())
                if (auto* b = dynamic_cast<juce::TextButton*>(grand)) {
                    EXPECT_TRUE(b->getTitle().isNotEmpty()) << b->getButtonText();
                    EXPECT_TRUE(b->getTooltip().isNotEmpty()) << b->getButtonText();
                }
    };
    auto manage = panel(AccountFlowPanel::Flow::manage);
    check(*manage);
    auto confirm = panel(AccountFlowPanel::Flow::deleteAccount);
    check(*confirm);
}

TEST_F(AccountFlowTest, DeleteButtonsTabInOrderCancelThenDelete) {
    build();
    auto p = panel(AccountFlowPanel::Flow::deleteAccount);
    juce::KeyboardFocusTraverser traverser;
    auto* first = traverser.getDefaultComponent(p.get());
    EXPECT_EQ(first, p->findButtonForTest("Cancel"));
    EXPECT_EQ(traverser.getNextComponent(first), p->findButtonForTest("Delete account"));
}
