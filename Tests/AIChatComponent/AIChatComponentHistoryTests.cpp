// AIChatComponentHistoryTests.cpp
// P6-8/P6-9 local multi-conversation history: the unified history UI, upsell/downgrade strips,
// backend selection per plan (local vs. cloud), clear-history, restoring a saved conversation,
// local save-on-every-exchange, rating sync to the server, and the wrapped-height UX-polish
// regressions that reuse this section's FakeHistorySource/configureTestAppProperties helpers.
// Shared mocks and the AIChatComponentTest fixture live in AIChatComponentTestFixture.h.
//
// Core component tests live in AIChatComponentTests.cpp; Arrange-mode tests live in
// AIChatComponentArrangeTests.cpp.

#include "AIChatComponentTestFixture.h"

// ============================================================================
// P6-8: local multi-conversation history + unified history UI + upsell/downgrade strips
// ============================================================================

namespace {

using synth::ConversationHistorySource;
using synth::LocalConversation;
using synth::LocalConversationSummary;

// A ConversationHistorySource whose every method answers synchronously and records what was
// asked of it — the seam AIChatComponent's setHistorySourcesForTesting() installs, standing in
// for both LocalHistorySource (real dir) and CloudHistorySource (real HTTP) so tests never touch
// disk or the network to exercise historyButtonClicked()'s backend-selection logic.
class FakeHistorySource : public ConversationHistorySource {
public:
    std::vector<LocalConversationSummary> conversationsToList;
    juce::String deletionScheduledAtToReport;
    bool listOk = true;

    LocalConversation conversationToReturn;
    bool getOk = true;

    bool deleteAllCalled = false;
    int deleteAllCountToReport = 0;

    void list(std::function<void(ListResult)> callback) override {
        ListResult result;
        result.ok = listOk;
        result.conversations = conversationsToList;
        result.deletionScheduledAt = deletionScheduledAtToReport;
        if (callback)
            callback(std::move(result));
    }

    void get(const juce::String&, std::function<void(bool, LocalConversation)> callback) override {
        if (callback)
            callback(getOk, conversationToReturn);
    }

    void deleteAll(std::function<void(bool, int)> callback) override {
        deleteAllCalled = true;
        if (callback)
            callback(true, deleteAllCountToReport);
    }
};

// juce::ApplicationProperties has no copy/move constructor, so this configures `props` in place
// rather than returning one by value (matching every pre-existing test in this file, which
// constructs its juce::PropertiesFile::Options inline for the same reason).
void configureTestAppProperties(juce::ApplicationProperties& props) {
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);
}

// Same idiom as PlanBadgeTests.cpp's FakeAuthServer — routes AuthClient's /v1/auth/token,
// /v1/auth/me, /v1/entitlement calls by URL suffix, enough for AccountService::completeSignIn().
synth::AuthClient::HttpResult makeStatus(int status, const juce::String& body) {
    synth::AuthClient::HttpResult result;
    result.httpStatus = status;
    result.body = body;
    return result;
}

synth::AuthClient::HttpResult makeTransportFailure() {
    synth::AuthClient::HttpResult result;
    result.transportFailed = true;
    result.errorMessage = "offline";
    return result;
}

synth::AuthClient::HttpResult makeMeSuccess() {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", "user-1");
    obj->setProperty("email", "jane@example.com");
    obj->setProperty("display_name", "Jane");
    obj->setProperty("created_at", "2024-01-01");
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makeEntitlementSuccess(const juce::String& plan) {
    juce::DynamicObject::Ptr usage = new juce::DynamicObject();
    usage->setProperty("requests_used", 0);
    usage->setProperty("period_start", "2026-08-01");
    juce::DynamicObject::Ptr limits = new juce::DynamicObject();
    limits->setProperty("monthly_requests", 1000);
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("plan", plan);
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

synth::AuthClient::HttpPerformer makeSignInPerformer(const juce::String& plan) {
    return [plan](const juce::String&, const juce::String& url, const juce::StringPairArray&, const juce::String&, int,
                  const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        if (url.endsWith("/v1/auth/token"))
            return makeTokenSuccess();
        if (url.endsWith("/v1/auth/me"))
            return makeMeSuccess();
        if (url.endsWith("/v1/entitlement"))
            return makeEntitlementSuccess(plan);
        return makeTransportFailure();
    };
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

// Signs `service` in (device-code-free: reuses the refresh-token grant, same as
// PlanBadgeTests.cpp) against `plan`, and blocks (via waitUntil) until entitlementKnown.
void signInWithPlan(synth::AccountService& service, const juce::String& plan) {
    service.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return service.getSnapshot().entitlementKnown; }));
    ASSERT_EQ(service.getSnapshot().plan.toLowerCase(), plan.toLowerCase());
    // publishSnapshot() updates the shared snapshot (what entitlementKnown above just observed)
    // synchronously on the worker thread, UNDER LOCK, but dispatches onStateChanged (which is what
    // actually drives AIChatComponent::updateUpsellStrip()/updateDowngradeStrip()) via a SEPARATE
    // MessageManager::callAsync — so entitlementKnown can flip true one dispatch-loop pump before
    // that callAsync is actually processed. Pump a bit more to flush it before asserting on
    // anything that callback updates.
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
}

// Direct child search (NOT recursive into messageList) — the upsell/downgrade strips are direct
// children of AIChatComponent itself, and searching from the root keeps this from ever colliding
// with a per-message-bubble "Upgrade to Pro" button (e.g. the Quota-error bubble), which lives
// inside messageList instead.
juce::TextButton* findDirectChildButton(synth::AIChatComponent& chat, const juce::String& text) {
    for (auto* child : chat.getChildren())
        if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == text)
                return button;
    return nullptr;
}

juce::Label* findDirectChildLabelContaining(synth::AIChatComponent& chat, const juce::String& substring) {
    for (auto* child : chat.getChildren())
        if (auto* label = dynamic_cast<juce::Label*>(child))
            if (label->getText().contains(substring))
                return label;
    return nullptr;
}

} // namespace

// ---- Upsell strip -----------------------------------------------------------------------

TEST_F(AIChatComponentTest, UpsellStripVisibleWithNoAccountService) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto* upsellButton = findDirectChildButton(chatComponent, "Upgrade to Pro");
    ASSERT_NE(upsellButton, nullptr);
    EXPECT_TRUE(upsellButton->isVisible())
        << "no AccountService at all must still show the upsell strip (every caller starts Free)";
}

TEST_F(AIChatComponentTest, UpsellStripHiddenOncePlanIsKnownPro) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);

    signInWithPlan(accountService, "pro");
    // signInWithPlan()'s waitUntil() pumps the dispatch loop until entitlementKnown is true, which
    // is also when AccountService::onStateChanged (AIChatComponent's own slot, calling
    // updateUpsellStrip()) has already fired via callAsync.

    auto* upsellButton = findDirectChildButton(chatComponent, "Upgrade to Pro");
    ASSERT_NE(upsellButton, nullptr);
    EXPECT_FALSE(upsellButton->isVisible());

    chatComponent.setAccountService(nullptr);
}

TEST_F(AIChatComponentTest, UpsellStripVisibleForSignedInFreePlan) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("free"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);

    signInWithPlan(accountService, "free");

    auto* upsellButton = findDirectChildButton(chatComponent, "Upgrade to Pro");
    ASSERT_NE(upsellButton, nullptr);
    EXPECT_TRUE(upsellButton->isVisible());

    chatComponent.setAccountService(nullptr);
}

TEST_F(AIChatComponentTest, HistoryButtonTooltipCarriesUpsellTextOnlyWhenNotPro) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    // No AccountService at all: still "not Pro" (see upsellButton's member doc comment), so the
    // tooltip should already carry the upsell text.
    EXPECT_TRUE(chatComponent.getHistoryButtonTooltipForTesting().contains("saved locally only"));

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);

    signInWithPlan(accountService, "pro");

    auto proTooltip = chatComponent.getHistoryButtonTooltipForTesting();
    EXPECT_FALSE(proTooltip.contains("saved locally only"));
    EXPECT_TRUE(proTooltip.contains("View, restore, or clear saved conversations"));

    chatComponent.setAccountService(nullptr);
}

// ---- History panel: backend selection per plan, and the downgrade strip -------------------

TEST_F(AIChatComponentTest, NotSignedInHistoryPanelListsFromLocalBackendOnly) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto localFake = std::make_unique<FakeHistorySource>();
    localFake->conversationsToList = {{"a", "Conv A", "2026-08-01T00:00:00.000Z", "2026-08-01T00:00:00.000Z"}};
    auto cloudFake = std::make_unique<FakeHistorySource>(); // must never be queried — no AccountService
    auto* cloudFakePtr = cloudFake.get();
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    chatComponent.simulateHistoryButtonClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);

    EXPECT_TRUE(chatComponent.didShowHistoryPopupForTesting());
    EXPECT_FALSE(chatComponent.lastHistoryPopupWasCloudForTesting());
    ASSERT_EQ(chatComponent.lastHistoryListForTesting().size(), 1u);
    EXPECT_EQ(chatComponent.lastHistoryListForTesting()[0].id, "a");
    EXPECT_FALSE(cloudFakePtr->deleteAllCalled) << "cloud backend must never be touched with no AccountService";
}

TEST_F(AIChatComponentTest, ProPlanHistoryPanelListsFromCloudBackend) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "pro");

    auto localFake = std::make_unique<FakeHistorySource>(); // must never be queried — plan is Pro
    auto cloudFake = std::make_unique<FakeHistorySource>();
    cloudFake->conversationsToList = {
        {"cloud-1", "Cloud Conv", "2026-08-01T00:00:00.000Z", "2026-08-02T00:00:00.000Z"}};
    cloudFake->deletionScheduledAtToReport = ""; // active Pro: no pending deletion
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    chatComponent.simulateHistoryButtonClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    EXPECT_TRUE(chatComponent.didShowHistoryPopupForTesting());
    EXPECT_TRUE(chatComponent.lastHistoryPopupWasCloudForTesting());
    ASSERT_EQ(chatComponent.lastHistoryListForTesting().size(), 1u);
    EXPECT_EQ(chatComponent.lastHistoryListForTesting()[0].id, "cloud-1");

    EXPECT_EQ(findDirectChildLabelContaining(chatComponent, "lapsed"), nullptr)
        << "an active Pro account must never show the downgrade strip";

    chatComponent.setAccountService(nullptr);
}

TEST_F(AIChatComponentTest, LapsedFreePlanListsLocallyButShowsDowngradeStripWithDate) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("free"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "free");

    auto localFake = std::make_unique<FakeHistorySource>();
    localFake->conversationsToList = {
        {"local-1", "Local Conv", "2026-08-01T00:00:00.000Z", "2026-08-02T00:00:00.000Z"}};
    auto cloudFake = std::make_unique<FakeHistorySource>();
    // The cloud call still happens (it's the only source of this date) even though the list it
    // returns is discarded in favour of the local one — see historyButtonClicked()'s doc comment.
    cloudFake->deletionScheduledAtToReport = "2026-09-15T00:00:00.000Z";
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    // No downgrade strip until the History click actually happens — never polled/speculative.
    EXPECT_EQ(findDirectChildLabelContaining(chatComponent, "lapsed"), nullptr);

    chatComponent.simulateHistoryButtonClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    EXPECT_TRUE(chatComponent.didShowHistoryPopupForTesting());
    EXPECT_FALSE(chatComponent.lastHistoryPopupWasCloudForTesting())
        << "Free plan must list from the LOCAL backend even though a cloud call was made for the date";
    ASSERT_EQ(chatComponent.lastHistoryListForTesting().size(), 1u);
    EXPECT_EQ(chatComponent.lastHistoryListForTesting()[0].id, "local-1");

    auto* downgradeLabel = findDirectChildLabelContaining(chatComponent, "lapsed");
    ASSERT_NE(downgradeLabel, nullptr);
    EXPECT_TRUE(downgradeLabel->isVisible());
    EXPECT_TRUE(downgradeLabel->getText().contains("2026")) << "the readable date must be rendered";

    chatComponent.setAccountService(nullptr);
}

// ---- "Clear my history" — wired to the plan-appropriate backend --------------------------

TEST_F(AIChatComponentTest, ClearHistoryOnFreePlanClearsLocalBackendOnly) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto localFake = std::make_unique<FakeHistorySource>();
    auto cloudFake = std::make_unique<FakeHistorySource>();
    auto* localPtr = localFake.get();
    auto* cloudPtr = cloudFake.get();
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    chatComponent.simulateClearHistoryConfirmed();

    EXPECT_TRUE(localPtr->deleteAllCalled);
    EXPECT_FALSE(cloudPtr->deleteAllCalled);
}

TEST_F(AIChatComponentTest, ClearHistoryOnProPlanClearsCloudBackendOnly) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "pro");

    auto localFake = std::make_unique<FakeHistorySource>();
    auto cloudFake = std::make_unique<FakeHistorySource>();
    auto* localPtr = localFake.get();
    auto* cloudPtr = cloudFake.get();
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    chatComponent.simulateClearHistoryConfirmed();

    EXPECT_TRUE(cloudPtr->deleteAllCalled);
    EXPECT_FALSE(localPtr->deleteAllCalled);

    chatComponent.setAccountService(nullptr);
}

// ---- Restoring a saved conversation --------------------------------------------------------

TEST_F(AIChatComponentTest, RestoringALocalConversationReplaysItsMessages) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto localFake = std::make_unique<FakeHistorySource>();
    localFake->conversationToReturn.id = "restore-me";
    localFake->conversationToReturn.title = "Old Conversation";
    localFake->conversationToReturn.createdAt = "2026-08-01T00:00:00.000Z";
    localFake->conversationToReturn.updatedAt = "2026-08-01T00:00:00.000Z";
    localFake->conversationToReturn.messages = {{"user", "restored user message", "2026-08-01T00:00:00.000Z"},
                                                {"assistant", "restored assistant reply", "2026-08-01T00:00:00.000Z"}};
    chatComponent.setHistorySourcesForTesting(std::move(localFake), nullptr);

    chatComponent.simulateRestoreConversationForTesting("restore-me", /*isCloud=*/false);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);
    EXPECT_NE(findDescendantWithText<juce::Label>(messageList, "restored user message"), nullptr);
    EXPECT_NE(findDescendantWithText<juce::Label>(messageList, "restored assistant reply"), nullptr);

    // aiService's own chatHistory was cleared (not re-seeded — see restoreConversation()'s doc
    // comment): only the system prompt remains.
    EXPECT_EQ(service.getHistory().size(), 1u);
}

// ---- Local save-on-every-exchange ----------------------------------------------------------

TEST_F(AIChatComponentTest, EverySuccessfulExchangeIsSavedLocallyRegardlessOfPlan) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto historyDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                          .getChildFile("AIChatComponentTest_History_" + juce::Uuid().toString());
    historyDir.deleteRecursively();
    chatComponent.setLocalHistoryDirectoryForTesting(historyDir);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);

    inputField->setText("hello there");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto list = synth::LocalHistoryStore::list(historyDir);
    ASSERT_EQ(list.size(), 1u) << "the first exchange must lazily create exactly one conversation file";

    synth::LocalConversation loaded;
    ASSERT_TRUE(synth::LocalHistoryStore::get(historyDir, list[0].id, loaded));
    ASSERT_EQ(loaded.messages.size(), 2u);
    EXPECT_EQ(loaded.messages[0].role, "user");
    EXPECT_TRUE(loaded.messages[0].content.contains("hello there"));
    EXPECT_EQ(loaded.messages[1].role, "assistant");

    // A second exchange in the SAME session appends to the SAME conversation id rather than
    // minting a new one.
    inputField->setText("second message");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto listAfterSecond = synth::LocalHistoryStore::list(historyDir);
    ASSERT_EQ(listAfterSecond.size(), 1u)
        << "same session must keep appending to one conversation, not create a second";
    synth::LocalConversation loadedAfterSecond;
    ASSERT_TRUE(synth::LocalHistoryStore::get(historyDir, listAfterSecond[0].id, loadedAfterSecond));
    EXPECT_EQ(loadedAfterSecond.messages.size(), 4u);

    historyDir.deleteRecursively();
}

// Regression lock: New Chat must clear AIIntegrationService's own (cloud) conversation id, not
// just this component's local one — otherwise a Pro user's next message after New Chat still
// carries the OLD x-conversation-id and the server appends onto the previous cloud thread while a
// fresh file starts locally, silently diverging the two.
TEST_F(AIChatComponentTest, NewChatClearsCloudConversationIdToo) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto providerPtr = std::make_unique<ConversationIdRecordingProvider>();
    auto* provider = providerPtr.get();
    service.setProvider(std::move(providerPtr));

    juce::ApplicationProperties props;
    configureTestAppProperties(props);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    juce::TextButton* newChatButton = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
        else if (auto* button = dynamic_cast<juce::TextButton*>(child))
            if (button->getButtonText() == "New Chat")
                newChatButton = button;
    }
    ASSERT_NE(inputField, nullptr);
    ASSERT_NE(newChatButton, nullptr);

    inputField->setText("hello");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    ASSERT_FALSE(provider->setConversationIdCalls.empty());
    EXPECT_EQ(provider->setConversationIdCalls.back(), "server-conv-1")
        << "setup failed: the response's conversationId should have been captured";

    newChatButton->onClick();

    ASSERT_FALSE(provider->setConversationIdCalls.empty());
    EXPECT_TRUE(provider->setConversationIdCalls.back().isEmpty())
        << "New Chat must clear the cloud conversation id, not just the local one";
}

// ---- P6-9: rating sync to the server -------------------------------------------------------

namespace {

// Observation state for a fake feedback HttpPerformer, held via shared_ptr so it stays alive for
// the detached background thread the rating callback fires — the thread's copy of the lambda (and
// therefore this shared_ptr) can easily outlive the TEST_F stack frame, so nothing here may be
// captured by reference. waitUntil() below pumps the message loop until callCount confirms the
// call actually landed before any assertion reads the captured fields.
struct CapturedFeedbackRequest {
    std::atomic<int> callCount{0};
    juce::String method;
    juce::String url;
    juce::StringPairArray headers;
    juce::String body;
};

synth::AuthClient::HttpPerformer makeFeedbackPerformer(std::shared_ptr<CapturedFeedbackRequest> captured,
                                                       int httpStatus = 200) {
    return [captured, httpStatus](const juce::String& method, const juce::String& url,
                                  const juce::StringPairArray& headers, const juce::String& body, int,
                                  const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        captured->method = method;
        captured->url = url;
        captured->headers = headers;
        captured->body = body;
        synth::AuthClient::HttpResult result;
        result.httpStatus = httpStatus;
        captured->callCount.fetch_add(1); // last write: callCount is the "call landed" signal
        return result;
    };
}

// Finds and clicks the thumbs-up button, driving the rating callback exactly like a real click.
void clickThumbsUp(synth::AIChatComponent& chatComponent) {
    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);
    auto* goodButton =
        findDescendantWithText<juce::TextButton>(messageList, juce::String::fromUTF8("\xF0\x9F\x91\x8D"));
    ASSERT_NE(goodButton, nullptr);
    goodButton->onClick();
}

} // namespace

TEST_F(AIChatComponentTest, RatingWithServerMessageIdAndProAccountFiresExactlyOneFeedbackPost) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockPatchProviderWithServerIds>());

    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto feedbackFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("AIChatComponentTest_" + juce::Uuid().toString())
                            .getChildFile("patch_feedback.jsonl");
    chatComponent.setPatchFeedbackFileForTesting(feedbackFile);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "pro");

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    chatComponent.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    clickThumbsUp(chatComponent);

    ASSERT_TRUE(waitUntil([&] { return captured->callCount.load() >= 1; }))
        << "feedback POST never landed on the background thread";
    EXPECT_EQ(captured->callCount.load(), 1);
    EXPECT_EQ(captured->method, juce::String("POST"));
    EXPECT_EQ(captured->url, juce::String(synth::branding::kApiBaseUrl) +
                                 "/v1/conversations/server-conv-1/messages/server-msg-1/feedback");
    EXPECT_EQ(captured->headers.getValue("Authorization", ""), juce::String("Bearer at1"));

    const auto parsedBody = juce::JSON::parse(captured->body);
    auto* bodyObj = parsedBody.getDynamicObject();
    ASSERT_NE(bodyObj, nullptr);
    EXPECT_EQ(bodyObj->getProperty("rating").toString(), juce::String("up"));

    chatComponent.setAccountService(nullptr);
    feedbackFile.getParentDirectory().deleteRecursively();
}

TEST_F(AIChatComponentTest, RatingOnFreePlanAccountDoesNotFireFeedbackPost) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockPatchProviderWithServerIds>());

    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto feedbackFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("AIChatComponentTest_" + juce::Uuid().toString())
                            .getChildFile("patch_feedback.jsonl");
    chatComponent.setPatchFeedbackFileForTesting(feedbackFile);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("free"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "free");

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    chatComponent.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // MockPatchProviderWithServerIds still returns a conversationId/messageId even against a Free
    // account here (a real hosted backend never would — see AIResponse::conversationId's doc
    // comment) so this test isolates ONLY the plan gate: serverMessageId is non-empty, yet the
    // account being Free must still suppress the POST.
    clickThumbsUp(chatComponent);

    // Give a wrongly-fired background thread a real chance to land before asserting its absence.
    juce::Thread::sleep(200);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(captured->callCount.load(), 0);

    chatComponent.setAccountService(nullptr);
    feedbackFile.getParentDirectory().deleteRecursively();
}

TEST_F(AIChatComponentTest, RatingWithNoServerMessageIdDoesNotFireFeedbackPostEvenWhenPro) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    // Plain MockPatchProvider: success, but no conversationId/messageId — mirrors a local Ollama
    // response, or any response with no server-side persistence.
    service.setProvider(std::make_unique<MockPatchProvider>());

    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto feedbackFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("AIChatComponentTest_" + juce::Uuid().toString())
                            .getChildFile("patch_feedback.jsonl");
    chatComponent.setPatchFeedbackFileForTesting(feedbackFile);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("http://mock-host:8787", makeSignInPerformer("pro"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "pro");

    auto captured = std::make_shared<CapturedFeedbackRequest>();
    chatComponent.setFeedbackHttpPerformerForTesting(makeFeedbackPerformer(captured));

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    clickThumbsUp(chatComponent);

    juce::Thread::sleep(200);
    juce::MessageManager::getInstance()->runDispatchLoopUntil(50);
    EXPECT_EQ(captured->callCount.load(), 0);

    chatComponent.setAccountService(nullptr);
    feedbackFile.getParentDirectory().deleteRecursively();
}

// ============================================================================
// UX polish: bubble/card wrapped-height regressions (AIChatComponent::computeWrappedTextHeight())
// ============================================================================

// Direct, headless coverage of the pure helper every variable-length text element in this panel
// now shares (PatchCard's diff/status box, hostedModeNotice, downgradeStripLabel) — see its header
// doc comment. A width wide enough for one line must need roughly one line of height; forcing the
// same text to wrap at a much narrower width must need several times that. The bug this replaced
// was a FIXED single-line/line-count estimate that never grew with the actual wrapped height.
TEST(AIChatComponentLayoutHelperTest, ComputeWrappedTextHeightGrowsWhenTextIsForcedToWrap) {
    const juce::Font font(14.0f);
    const juce::String longText = "Preview unavailable - this patch may be rejected when applied.";

    const int oneLineHeight = synth::AIChatComponent::computeWrappedTextHeight(font, longText, 2000);
    const int wrappedHeight = synth::AIChatComponent::computeWrappedTextHeight(font, longText, 80);

    EXPECT_LE(oneLineHeight, (int)std::ceil(font.getHeight()) + 4);
    EXPECT_GT(wrappedHeight, oneLineHeight * 2)
        << "a long line forced to wrap across several rows at a narrow width must reserve "
           "several times a single line's height, not the same fixed estimate";
}

TEST(AIChatComponentLayoutHelperTest, ComputeWrappedTextHeightIsNeverLessThanOneLine) {
    const juce::Font font(12.0f);
    EXPECT_GE(synth::AIChatComponent::computeWrappedTextHeight(font, "", 200), (int)font.getHeight());
    EXPECT_GE(synth::AIChatComponent::computeWrappedTextHeight(font, "short", 200), (int)font.getHeight());
}

// Full-integration regression for the "Preview unavailable..." clipping bug: a real PatchCard
// (reached only through the private MessageBubble it's nested inside) whose diffAvailable is
// false must reserve enough height in the rendered TextEditor to show that status line in full,
// not a fixed single-line box.
TEST_F(AIChatComponentTest, PatchCardPreviewUnavailableStatusIsNotClipped) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockInvalidPatchProvider>());

    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    // Narrow panel so the status line is forced to wrap across more than one row inside
    // PatchCard's diff/status TextEditor — the case a line-count estimate used to clip.
    chatComponent.setSize(260, 600);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren())
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);

    // findDescendantWithText() only covers Label/TextButton, and the status line renders inside a
    // juce::TextEditor — walk the tree directly for it.
    juce::TextEditor* statusEditor = nullptr;
    std::function<void(juce::Component*)> findStatusEditor = [&](juce::Component* c) {
        if (c == nullptr || statusEditor != nullptr)
            return;
        if (auto* editor = dynamic_cast<juce::TextEditor*>(c);
            editor != nullptr && editor->getText().contains("Preview unavailable")) {
            statusEditor = editor;
            return;
        }
        for (auto* child : c->getChildren())
            findStatusEditor(child);
    };
    findStatusEditor(messageList);
    ASSERT_NE(statusEditor, nullptr);

    const int wrappedHeight = synth::AIChatComponent::computeWrappedTextHeight(
        statusEditor->getFont(), statusEditor->getText(), statusEditor->getWidth());
    EXPECT_GE(statusEditor->getHeight(), wrappedHeight)
        << "the status box must be tall enough to show its full wrapped text, not a fixed "
           "single-line estimate";
    // Confirms the width really did force a wrap (otherwise the assertion above would pass
    // trivially even under the old, buggy line-count estimate).
    EXPECT_GT(wrappedHeight, (int)std::ceil(statusEditor->getFont().getHeight()) + 4);
}

// Full-integration regression for the bottom-bar hint-reservation bug: downgradeStripLabel's text
// embeds a variable-length date and can wrap at this panel's width, and a fixed single-line
// reservation truncated it. AIChatComponent::resized() must reserve at least the label's own
// measured wrapped height.
TEST_F(AIChatComponentTest, BottomBarReservesFullHeightForWrappedDowngradeNotice) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());
    juce::ApplicationProperties props;
    configureTestAppProperties(props);
    synth::AIChatComponent chatComponent(service, props);
    // Narrow panel so the downgrade sentence is forced to wrap.
    chatComponent.setSize(260, 600);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService("https://mock-host:8787", makeSignInPerformer("free"), std::move(tokenStore));
    chatComponent.setAccountService(&accountService);
    signInWithPlan(accountService, "free");

    auto localFake = std::make_unique<FakeHistorySource>();
    auto cloudFake = std::make_unique<FakeHistorySource>();
    cloudFake->deletionScheduledAtToReport = "2026-09-15T00:00:00.000Z";
    chatComponent.setHistorySourcesForTesting(std::move(localFake), std::move(cloudFake));

    chatComponent.simulateHistoryButtonClick();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* downgradeLabel = findDirectChildLabelContaining(chatComponent, "lapsed");
    ASSERT_NE(downgradeLabel, nullptr);
    ASSERT_TRUE(downgradeLabel->isVisible());

    const int wrappedHeight = synth::AIChatComponent::computeWrappedTextHeight(
        downgradeLabel->getFont(), downgradeLabel->getText(), downgradeLabel->getWidth());
    EXPECT_GE(downgradeLabel->getHeight(), wrappedHeight)
        << "the downgrade notice must reserve its full wrapped height, not a fixed one-line guess";
    EXPECT_GT(wrappedHeight, (int)std::ceil(downgradeLabel->getFont().getHeight()) + 4)
        << "the notice's text must actually need more than one line at this width, or the "
           "assertion above passes trivially";

    chatComponent.setAccountService(nullptr);
}
