#include "../Source/AI/AccountService.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/Branding.h"
#include "../Source/UI/FeedbackSettingsTab.h"
#include <chrono>
#include <cstdlib>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <memory>

namespace {

// setenv()/unsetenv() are POSIX-only (MSVC has neither) -- mirrors BrandingTests.cpp's own
// portable helpers exactly, kept local since that file's aren't exported/shared.
#ifdef _WIN32
void setTestEnv(const char* name, const char* value) { _putenv_s(name, value); }
void clearTestEnv(const char* name) { _putenv_s(name, ""); }
#else
void setTestEnv(const char* name, const char* value) { setenv(name, value, 1); }
void clearTestEnv(const char* name) { unsetenv(name); }
#endif

// ---- P6-16: sync test doubles, local to this file (mirrors AIChatComponentTests.cpp's own
// copies -- those are not exported/shared) --------------------------------------------------

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

synth::AuthClient::HttpResult makeStatus(int status, const juce::String& body) {
    synth::AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    return result;
}

synth::AuthClient::HttpResult makeMeSuccess() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", "user-1");
    obj->setProperty("email", "user@example.com");
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makeEntitlementSuccess() {
    juce::DynamicObject::Ptr usage = new juce::DynamicObject();
    usage->setProperty("requests_used", 0);
    usage->setProperty("period_start", "2026-08-01");
    juce::DynamicObject::Ptr limits = new juce::DynamicObject();
    limits->setProperty("monthly_requests", 1000);
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("plan", "free");
    obj->setProperty("status", "active");
    obj->setProperty("period_end", juce::var());
    obj->setProperty("cancel_at_period_end", false);
    obj->setProperty("limits", juce::var(limits.get()));
    obj->setProperty("usage", juce::var(usage.get()));
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makeTokenSuccess() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("access_token", "at1");
    obj->setProperty("token_type", "Bearer");
    obj->setProperty("expires_in", 3600);
    obj->setProperty("refresh_token", "rt1");
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makeTransportFailure() {
    synth::AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = "Could not resolve host";
    return result;
}

// General feedback doesn't care about plan -- this always signs in as "free", unlike
// AIChatComponentTests.cpp's parameterised makeSignInPerformer().
synth::AuthClient::HttpPerformer makeSignInPerformer() {
    return [](const juce::String&, const juce::String& url, const juce::StringPairArray&, const juce::String&, int,
              const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        if (url.endsWith("/v1/auth/token"))
            return makeTokenSuccess();
        if (url.endsWith("/v1/auth/me"))
            return makeMeSuccess();
        if (url.endsWith("/v1/entitlement"))
            return makeEntitlementSuccess();
        return makeTransportFailure();
    };
}

// Signs `service` in (device-code-free: reuses the refresh-token grant, same as
// AIChatComponentTests.cpp's signInWithPlan) and blocks until entitlementKnown.
void signIn(synth::AccountService& service) {
    service.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    ASSERT_EQ(service.getSnapshot().state, synth::AccountState::SignedIn);
}

// Observation state for a fake feedback HttpPerformer, held via shared_ptr so it stays alive for
// the detached background thread sendFeedback() fires -- the thread's copy of the lambda (and
// therefore this shared_ptr) can easily outlive the TEST_F stack frame, so nothing here may be
// captured by reference.
struct CapturedFeedbackRequest {
    std::atomic<int> callCount{0};
    juce::String method;
    juce::String url;
    juce::StringPairArray headers;
    juce::String body;
};

synth::AuthClient::HttpPerformer makeFeedbackPerformer(std::shared_ptr<CapturedFeedbackRequest> captured) {
    return [captured](const juce::String& method, const juce::String& url, const juce::StringPairArray& headers,
                      const juce::String& body, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        captured->method = method;
        captured->url = url;
        captured->headers = headers;
        captured->body = body;
        synth::AuthClient::HttpResult result;
        result.httpStatus = 200;
        captured->callCount.fetch_add(1); // last write: callCount is the "call landed" signal
        return result;
    };
}

} // namespace

class FeedbackSettingsTabTest : public ::testing::Test {
protected:
    // Clearing AGENTSYNTH_LOCAL_API_URL here too (not just in the dedicated test below) keeps it
    // from leaking into every other test in this fixture, or into any other test file run in the
    // same process -- same reasoning as BrandingTests.cpp's ResolveApiBaseUrlTest fixture.
    void SetUp() override {
        tempFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                       .getChildFile("FeedbackSettingsTabTest_" + juce::Uuid().toString())
                       .getChildFile("general_feedback.jsonl");
        clearTestEnv("AGENTSYNTH_LOCAL_API_URL");
    }

    void TearDown() override {
        tempFile.getParentDirectory().deleteRecursively();
        clearTestEnv("AGENTSYNTH_LOCAL_API_URL");
    }

    juce::StringArray lines() const {
        juce::StringArray result;
        result.addLines(tempFile.loadFileAsString());
        result.removeEmptyStrings();
        return result;
    }

    juce::File tempFile;
};

TEST_F(FeedbackSettingsTabTest, SendButtonStartsDisabled) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, TypingTextEnablesSendButton) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("something broke", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_TRUE(tab.getSendButtonForTest().isEnabled());

    tab.getFeedbackEditorForTest().setText("", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, WhitespaceOnlyTextDoesNotEnableSendButton) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("   \n  ", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
}

TEST_F(FeedbackSettingsTabTest, ClickingSendRecordsToStoreAndClearsEditor) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("please add MIDI export", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getCategoryComboForTest().setSelectedId(2, juce::sendNotificationSync); // Feature Request
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    EXPECT_TRUE(tab.getFeedbackEditorForTest().getText().isEmpty());
    EXPECT_FALSE(tab.getSendButtonForTest().isEnabled());
    EXPECT_TRUE(tab.getStatusTextForTest().isNotEmpty());

    ASSERT_EQ(lines().size(), 1);
    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getProperty("text").toString(), "please add MIDI export");
    EXPECT_EQ(entry->getProperty("category").toString(), "feature");
}

TEST_F(FeedbackSettingsTabTest, EditingAfterSendClearsStatusMessage) {
    FeedbackSettingsTab tab;
    tab.setFeedbackFileForTesting(tempFile);

    tab.getFeedbackEditorForTest().setText("first note", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    ASSERT_TRUE(tab.getStatusTextForTest().isNotEmpty());

    tab.getFeedbackEditorForTest().setText("second note", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    EXPECT_TRUE(tab.getStatusTextForTest().isEmpty());
}

TEST_F(FeedbackSettingsTabTest, ResizingDoesNotCrash) {
    FeedbackSettingsTab tab;
    tab.setSize(500, 450);
    EXPECT_NO_THROW(tab.setSize(800, 600));
    EXPECT_NO_THROW(tab.resized());
}

// ---- P6-16: sync to the server ---------------------------------------------------------------

TEST_F(FeedbackSettingsTabTest, SignedInUserSyncsFeedbackToServer) {
    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer(), std::move(tokenStore));
    signIn(accountService);

    FeedbackSettingsTab tab(&accountService);
    tab.setFeedbackFileForTesting(tempFile);

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    tab.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    tab.getFeedbackEditorForTest().setText("please add MIDI export", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getCategoryComboForTest().setSelectedId(2, juce::sendNotificationSync); // Feature Request
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    ASSERT_TRUE(waitUntil([&] { return captured->callCount.load() >= 1; }))
        << "feedback POST never landed on the background thread";
    EXPECT_EQ(captured->callCount.load(), 1);
    EXPECT_EQ(captured->method, juce::String("POST"));
    EXPECT_TRUE(captured->url.endsWith("/v1/feedback")) << captured->url;
    EXPECT_EQ(captured->headers.getValue("Authorization", ""), juce::String("Bearer at1"));

    const auto parsedBody = juce::JSON::parse(captured->body);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_EQ(bodyObj->getProperty("category").toString(), juce::String("feature"));
    EXPECT_EQ(bodyObj->getProperty("text").toString(), juce::String("please add MIDI export"));

    // Local log still got the entry regardless of the sync outcome.
    ASSERT_EQ(lines().size(), 1);
}

#ifndef NDEBUG
// Pins the one deliberate difference from P6-9's submitMessageFeedback sync (which uses the
// hardcoded synth::branding::kApiBaseUrl): this sync path must go through resolveApiBaseUrl(), so
// AGENTSYNTH_LOCAL_API_URL redirects it exactly like every other cloud-gated feature (see
// docs/testing.md "Testing Cloud-Gated Features Locally"). Only asserting url.endsWith("/v1/
// feedback") elsewhere wouldn't catch a regression to the hardcoded constant, since both resolve
// to the same production host by default -- this test only compiles/runs in Debug builds, since
// the override itself is compiled out of Release (see resolveApiBaseUrl()'s doc comment).
TEST_F(FeedbackSettingsTabTest, SyncUsesLocalApiUrlOverrideWhenSet) {
    setTestEnv("AGENTSYNTH_LOCAL_API_URL", "http://localhost:9999");

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer(), std::move(tokenStore));
    signIn(accountService);

    FeedbackSettingsTab tab(&accountService);
    tab.setFeedbackFileForTesting(tempFile);

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    tab.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    tab.getFeedbackEditorForTest().setText("local server test", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);

    ASSERT_TRUE(waitUntil([&] { return captured->callCount.load() >= 1; }));
    EXPECT_EQ(captured->url, juce::String("http://localhost:9999/v1/feedback"));
}
#endif

TEST_F(FeedbackSettingsTabTest, NotSignedInDoesNotAttemptSync) {
    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer(), std::move(tokenStore));
    // Deliberately not signed in: no attemptSilentSignIn()/signIn() call, so the account stays
    // SignedOut.

    FeedbackSettingsTab tab(&accountService);
    tab.setFeedbackFileForTesting(tempFile);

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    tab.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    tab.getFeedbackEditorForTest().setText("crashes on launch", juce::sendNotificationSync);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    tab.getSendButtonForTest().triggerClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(200);

    EXPECT_EQ(captured->callCount.load(), 0) << "no sync should be attempted while signed out";

    // Local log still got the entry.
    ASSERT_EQ(lines().size(), 1);
    auto entryVar = juce::JSON::parse(lines()[0]);
    auto* entry = entryVar.getDynamicObject();
    ASSERT_NE(entry, nullptr);
    EXPECT_EQ(entry->getProperty("text").toString(), "crashes on launch");
}
