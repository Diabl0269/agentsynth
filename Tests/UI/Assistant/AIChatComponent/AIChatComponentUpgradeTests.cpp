// AIChatComponentUpgradeTests.cpp -- the "Upgrade to Pro" buttons open the Polar checkout with the
// signed-in account's email prefilled (buildUpgradeUrl), and the upsell strip's tooltip wording.
#include "AI/UpgradeUrl.h"
#include "AIChatComponentTestFixture.h"
#include "Auth/InMemoryTokenStore.h"
#include <chrono>

namespace {
juce::String checkoutBase() { return juce::String(synth::branding::kUpgradeUrl); }

synth::AuthClient::HttpResult jsonResult(int status, const juce::String& body) {
    synth::AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    return result;
}

// Answers just enough of the account API for a silent sign-in as `email`; every other endpoint
// (entitlement, ...) fails at the transport layer, which sign-in tolerates.
synth::AuthClient::HttpPerformer signedInAs(const juce::String& email) {
    return [email](const juce::String&, const juce::String& url, const juce::StringPairArray&, const juce::String&, int,
                   const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        if (url.endsWith("/v1/auth/token"))
            return jsonResult(200, R"({"access_token":"at","token_type":"Bearer","expires_in":3600,)"
                                   R"("refresh_token":"rt"})");
        if (url.endsWith("/v1/auth/me"))
            return jsonResult(200, "{\"id\":\"u1\",\"email\":" + juce::JSON::toString(email) +
                                       ",\"display_name\":\"T\",\"created_at\":\"2024-01-01\"}");
        synth::AuthClient::HttpResult failed;
        failed.transportFailed = true;
        failed.errorMessage = "not used by this test";
        return failed;
    };
}
} // namespace

TEST(UpgradeUrlTest, EmptyEmailGivesTheBareCheckoutLink) {
    EXPECT_EQ(synth::buildUpgradeUrl({}).toString(true), checkoutBase());
    EXPECT_EQ(synth::buildUpgradeUrl("   ").toString(true), checkoutBase());
}

TEST(UpgradeUrlTest, EmailIsUrlEncodedIntoCustomerEmail) {
    const auto url = synth::buildUpgradeUrl("jane+synth@example.com");
    const juce::String text = url.toString(true);
    EXPECT_TRUE(text.startsWith(checkoutBase() + "?customer_email=")) << text;
    // '+' must be percent-encoded (a bare '+' decodes to a space) and '@' must not break the query.
    EXPECT_TRUE(text.contains("jane%2Bsynth%40example.com")) << text;
    EXPECT_FALSE(text.substring(text.indexOfChar('?')).containsChar('+')) << text;
    EXPECT_EQ(url.getParameterValues()[0], "jane+synth@example.com");
}

TEST_F(AIChatComponentTest, UpsellButtonOpensCheckoutWithSignedInEmail) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    // Declared before chatComponent so it outlives it (the component clears callback slots on it).
    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", signedInAs("jane+synth@example.com"),
                                         std::move(tokenStore));

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::URL openedUrl;
    bool opened = false;
    chatComponent.setUrlOpenerForTesting([&](const juce::URL& url) {
        openedUrl = url;
        opened = true;
    });

    auto* upsell = findDescendantWithText<juce::TextButton>(&chatComponent, "Upgrade to Pro");
    ASSERT_NE(upsell, nullptr);
    EXPECT_EQ(upsell->getTooltip(), "See the Pro plan: more hosted AI requests and cloud backup of your chat "
                                    "history. Hosted AI is in early access and improves with every update.");

    // No AccountService attached: the bare link.
    upsell->onClick();
    ASSERT_TRUE(opened);
    EXPECT_EQ(openedUrl.toString(true), checkoutBase());

    chatComponent.setAccountService(&accountService);
    accountService.attemptSilentSignIn();
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (accountService.getSnapshot().state != synth::AccountState::SignedIn &&
           std::chrono::steady_clock::now() < deadline)
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    ASSERT_EQ(accountService.getSnapshot().state, synth::AccountState::SignedIn);

    opened = false;
    upsell->onClick();
    ASSERT_TRUE(opened);
    EXPECT_EQ(openedUrl.toString(true), synth::buildUpgradeUrl("jane+synth@example.com").toString(true));
    EXPECT_EQ(openedUrl.getParameterValues()[0], "jane+synth@example.com");
}

// Bug: the requests counter stayed at its sign-in value because the entitlement was re-read only
// after a Quota error. Every finished hosted reply must re-read it.
TEST_F(AIChatComponentTest, EveryFinishedReplyRereadsTheEntitlement) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    auto entitlementFetches = std::make_shared<std::atomic<int>>(0);
    auto performer = [entitlementFetches](const juce::String&, const juce::String& url, const juce::StringPairArray&,
                                          const juce::String&, int,
                                          const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        if (url.endsWith("/v1/auth/token"))
            return jsonResult(200, R"({"access_token":"at","token_type":"Bearer","expires_in":3600,)"
                                   R"("refresh_token":"rt"})");
        if (url.endsWith("/v1/auth/me"))
            return jsonResult(200, R"({"id":"u1","email":"a@b.c","display_name":"T","created_at":"2024-01-01"})");
        if (url.endsWith("/v1/entitlement")) {
            const int used = ++*entitlementFetches;
            return jsonResult(200, R"({"plan":"pro","status":"active","period_end":null,)"
                                   R"("cancel_at_period_end":false,"limits":{"monthly_requests":1000},)"
                                   R"("usage":{"requests_used":)" +
                                       juce::String(used) + R"(,"period_start":"2026-08-01"}})");
        }
        synth::AuthClient::HttpResult failed;
        failed.transportFailed = true;
        return failed;
    };

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", performer, std::move(tokenStore));

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);
    chatComponent.setAccountService(&accountService);
    accountService.attemptSilentSignIn();

    const auto waitFor = [](auto predicate) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!predicate() && std::chrono::steady_clock::now() < deadline)
            juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
        return predicate();
    };
    ASSERT_TRUE(waitFor([&] { return accountService.getSnapshot().entitlementKnown; }));
    const int usedAfterSignIn = accountService.getSnapshot().requestsUsed;

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);

    inputField->setText("hello");
    chatComponent.triggerSend();

    EXPECT_TRUE(waitFor([&] { return accountService.getSnapshot().requestsUsed > usedAfterSignIn; }))
        << "the used-requests count never moved after a successful reply";
}
