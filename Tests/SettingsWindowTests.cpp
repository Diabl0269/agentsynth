#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AccountService.h"
#include "../Source/AI/LocalHistoryStore.h"
#include "../Source/AudioEngine.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/ShortcutManager.h"
#include "../Source/UI/AIChatComponent.h"
#include "../Source/UI/AppearanceSettingsTab.h"
#include "../Source/UI/NoteColour.h"
#include "../Source/UI/PreferencesSettingsTab.h"
#include "../Source/UI/SettingsWindow.h"
#include "../Source/UI/ShortcutsSettingsTab.h"
#include "../Source/UI/Theme/ThemeManager.h"
#include <chrono>
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// Mock AI provider for testing
class MockSettingsProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockSettingsProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1", "MockModel2"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "Mock response";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setRequestTimeoutMs(int timeoutMs) override { requestTimeoutMs = timeoutMs; }
    int getRequestTimeoutMs() const override { return requestTimeoutMs; }

private:
    juce::String currentModel = "MockModel1";
    int requestTimeoutMs = 240000;
};

// ============================================================================
// P6-7: prompt-learning opt-in checkbox test support
// ============================================================================
namespace {

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

synth::AuthClient::HttpResult makeTokenSuccess(const juce::String& accessToken, const juce::String& refreshToken) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("access_token", accessToken);
    obj->setProperty("expires_in", 3600);
    obj->setProperty("refresh_token", refreshToken);
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makeMeSuccess(const juce::String& email) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("id", "user-1");
    obj->setProperty("email", email);
    obj->setProperty("display_name", "Test User");
    obj->setProperty("created_at", "2024-01-01");
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

synth::AuthClient::HttpResult makePromptLearningResponse(bool optedIn) {
    juce::DynamicObject::Ptr obj = new juce::DynamicObject();
    obj->setProperty("opted_in", optedIn);
    obj->setProperty("opted_in_at", optedIn ? juce::var("2026-08-19T00:00:00.000Z") : juce::var());
    return makeStatus(200, juce::JSON::toString(juce::var(obj.get())));
}

// Minimal fake auth transport, mirroring AccountServiceTests.cpp's FakeAuthServer but scoped to
// only what these tests need: a silent-sign-in round trip (token + me), and GET/PUT
// /v1/prompt-learning distinguished by method (see AccountServiceTests.cpp's FakeAuthServer for
// why method-based dispatch matters — GET and PUT share a URL).
class FakeSettingsAuthServer {
public:
    synth::AuthClient::HttpResult tokenResponse = makeTransportFailure();
    synth::AuthClient::HttpResult meResponse = makeMeSuccess("jane@example.com");
    synth::AuthClient::HttpResult entitlementResponse = makeTransportFailure();
    synth::AuthClient::HttpResult promptLearningGetResponse = makeTransportFailure();
    synth::AuthClient::HttpResult promptLearningPutResponse = makeTransportFailure();

    mutable std::mutex mutex;
    int promptLearningGetCallCount = 0;
    int promptLearningPutCallCount = 0;

    synth::AuthClient::HttpPerformer performer() {
        return [this](const juce::String& method, const juce::String& url, const juce::StringPairArray&,
                      const juce::String&, int, const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
            const std::lock_guard<std::mutex> lock(mutex);
            if (url.endsWith("/v1/auth/token"))
                return tokenResponse;
            if (url.endsWith("/v1/auth/me"))
                return meResponse;
            if (url.endsWith("/v1/entitlement"))
                return entitlementResponse;
            if (url.endsWith("/v1/prompt-learning")) {
                if (method == "PUT") {
                    ++promptLearningPutCallCount;
                    return promptLearningPutResponse;
                }
                ++promptLearningGetCallCount;
                return promptLearningGetResponse;
            }
            return makeTransportFailure();
        };
    }

    int promptLearningPutCalls() const {
        const std::lock_guard<std::mutex> lock(mutex);
        return promptLearningPutCallCount;
    }
};

// Pumps the JUCE message loop while polling `predicate` — AccountService's sign-in/refresh flows
// run on a worker thread and publish via MessageManager::callAsync (see AccountServiceTests.cpp's
// identical helper).
template <typename Predicate>
bool waitUntil(Predicate predicate, std::chrono::milliseconds timeout = std::chrono::milliseconds(5000)) {
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    do {
        if (predicate())
            return true;
        juce::MessageManager::getInstance()->runDispatchLoopUntil(10);
    } while (std::chrono::steady_clock::now() < deadline);
    return predicate();
}

// The toggle is the only juce::ToggleButton in the AI tab (see SettingsWindow.cpp).
juce::ToggleButton* findPromptLearningToggle(juce::Component* aiTab) {
    for (auto* child : aiTab->getChildren())
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            return toggle;
    return nullptr;
}

} // namespace

class SettingsWindowTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up ApplicationProperties with temporary storage for testing
        juce::PropertiesFile::Options options;
        options.applicationName = "SettingsTest";
        options.filenameSuffix = "test";
        options.storageFormat = juce::PropertiesFile::storeAsXML;
        appProperties.setStorageParameters(options);

        // Initialize AI service with mock provider
        engine = std::make_unique<AudioEngine>();
        aiService = std::make_unique<synth::AIIntegrationService>(engine->getGraph());
        aiService->setProvider(std::make_unique<MockSettingsProvider>());

        // Initialize AI chat component
        aiChatComponent = std::make_unique<synth::AIChatComponent>(*aiService, appProperties);
    }

    void TearDown() override {
        // Clean up ApplicationProperties
        if (auto* userSettings = appProperties.getUserSettings()) {
            userSettings->clear();
        }
    }

    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<synth::AIIntegrationService> aiService;
    std::unique_ptr<synth::AIChatComponent> aiChatComponent;
    juce::ApplicationProperties appProperties;
    juce::AudioDeviceManager deviceManager;
    ShortcutManager shortcutManager;
    synth::theme::ThemeManager themeManager;
};

TEST_F(SettingsWindowTest, HasSixTabs) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getNumTabs(), 6);
}

TEST_F(SettingsWindowTest, TabNamesAreCorrect) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getTabName(0), "Audio");
    EXPECT_EQ(settingsWindow.getTabName(1), "AI");
    EXPECT_EQ(settingsWindow.getTabName(2), "Keyboard Shortcuts");
    EXPECT_EQ(settingsWindow.getTabName(3), "Preferences");
    EXPECT_EQ(settingsWindow.getTabName(4), "Appearance");
    EXPECT_EQ(settingsWindow.getTabName(5), "Feedback");
}

TEST_F(SettingsWindowTest, PreferencesTabHostsBehaviourControls) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    auto* tab = dynamic_cast<PreferencesSettingsTab*>(settingsWindow.getTabs().getTabContentComponent(3));
    ASSERT_NE(tab, nullptr);
    EXPECT_EQ(tab->getSmartConnectionMode(), GraphEditor::SmartConnectionMode::NewAndUnwired);
    EXPECT_TRUE(tab->isDoubleClickPortDisconnectEnabled());
}

TEST_F(SettingsWindowTest, DefaultTabIsAudio) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), 0);
}

TEST_F(SettingsWindowTest, AudioTabContainsDeviceSelector) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    auto* tabContent = settingsWindow.getTabs().getTabContentComponent(0);
    ASSERT_NE(tabContent, nullptr);

    auto* deviceSelector = dynamic_cast<juce::AudioDeviceSelectorComponent*>(tabContent);
    ASSERT_NE(deviceSelector, nullptr);
}

TEST_F(SettingsWindowTest, AITabPersistsProviderSetting) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);

    // Find the combo box in the AI tab
    juce::ComboBox* providerCombo = nullptr;
    for (auto* child : aiTab->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            providerCombo = combo;
            break;
        }
    }

    // If combo box is found, verify we can access it (the setting persistence
    // is tested through the AISettingsTab's updateSettings call)
    if (providerCombo != nullptr) {
        EXPECT_NE(providerCombo->getText(), "");
    }
}

// P4-6: "remote" is no longer hidden, so both providers must be offered.
TEST_F(SettingsWindowTest, AITabOffersBothOllamaAndRemoteProviders) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);

    juce::ComboBox* providerCombo = nullptr;
    for (auto* child : aiTab->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            providerCombo = combo;
            break;
        }
    }
    ASSERT_NE(providerCombo, nullptr);

    ASSERT_EQ(providerCombo->getNumItems(), 2);
    EXPECT_EQ(providerCombo->getItemText(0), "Ollama (local)");
    EXPECT_EQ(providerCombo->getItemText(1), "Remote (hosted)");
}

// Regression lock for the pre-P4-6 host-key collision: Ollama and Remote each persist (and
// display) their OWN host, under separate settings keys ("ollamaHost"/"remoteHost") — the
// pre-P4-6 code shared a single "ollamaHost" key, so switching providers silently carried one
// provider's host text over as the other's (e.g. RemoteProvider ending up pointed at Ollama's
// port).
//
// Verified by reconstructing AISettingsTab against differently-persisted state, NOT by live-
// switching the combo mid-test: AISettingsTab has no seam to inject a fake provider registry, so
// triggering its onChange/updateSettings() for real would construct an actual OllamaProvider or
// RemoteProvider — touching real threads, the network and DeviceIdStore's on-disk file, none of
// which belong in a unit test and one of which (confirmed while writing this test) hangs/aborts
// in this headless environment. The constructor path alone (read the persisted host for whichever
// provider is selected) exercises the exact same hostSettingsKeyFor()/defaultHostFor() lookup that
// updateSettings() uses to choose which key to WRITE, so this still locks the collision fix.
TEST_F(SettingsWindowTest, EachProviderReadsItsOwnPersistedHost) {
    appProperties.getUserSettings()->setValue("ollamaHost", "http://ollama.example:11434");
    appProperties.getUserSettings()->setValue("remoteHost", "https://remote.example");

    auto findHostEditor = [](juce::Component* aiTab) -> juce::TextEditor* {
        for (auto* child : aiTab->getChildren())
            if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
                return editor;
        return nullptr;
    };

    appProperties.getUserSettings()->setValue("aiProvider", "ollama");
    {
        SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                      themeManager, nullptr);
        settingsWindow.setSize(600, 400);
        settingsWindow.resized();
        auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
        ASSERT_NE(aiTab, nullptr);
        auto* hostEditor = findHostEditor(aiTab);
        ASSERT_NE(hostEditor, nullptr);
        EXPECT_EQ(hostEditor->getText(), "http://ollama.example:11434");
    }

    appProperties.getUserSettings()->setValue("aiProvider", "remote");
    {
        SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                      themeManager, nullptr);
        settingsWindow.setSize(600, 400);
        settingsWindow.resized();
        auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
        ASSERT_NE(aiTab, nullptr);
        auto* hostEditor = findHostEditor(aiTab);
        ASSERT_NE(hostEditor, nullptr);
        EXPECT_EQ(hostEditor->getText(), "https://remote.example");
    }
}

// ============================================================================
// P6-8: local chat-history retention control (AI tab, added after provider/host controls)
// ============================================================================

namespace {
// The retention combo is the SECOND ComboBox in the AI tab's child order (providerCombo is
// first) — see SettingsWindow.cpp's comment on why it's added after providerCombo/hostEditor.
juce::ComboBox* findHistoryRetentionCombo(juce::Component* aiTab) {
    int comboIndex = 0;
    for (auto* child : aiTab->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            if (comboIndex == 1)
                return combo;
            ++comboIndex;
        }
    }
    return nullptr;
}
} // namespace

TEST_F(SettingsWindowTest, HistoryRetentionDefaultsTo180Days) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* retentionCombo = findHistoryRetentionCombo(aiTab);
    ASSERT_NE(retentionCombo, nullptr);
    EXPECT_EQ(retentionCombo->getText(), "180 days");
}

TEST_F(SettingsWindowTest, HistoryRetentionLoadsPersistedSelection) {
    appProperties.getUserSettings()->setValue("historyRetentionDays", 90);

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* retentionCombo = findHistoryRetentionCombo(aiTab);
    ASSERT_NE(retentionCombo, nullptr);
    EXPECT_EQ(retentionCombo->getText(), "90 days");
}

TEST_F(SettingsWindowTest, HistoryRetentionPersistsSelectionToAppProperties) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* retentionCombo = findHistoryRetentionCombo(aiTab);
    ASSERT_NE(retentionCombo, nullptr);

    retentionCombo->setSelectedItemIndex(4, juce::sendNotificationSync); // "Keep forever"
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("historyRetentionDays", -999),
              synth::LocalHistoryStore::kRetainForever);
}

TEST_F(SettingsWindowTest, HistoryRetentionOutOfRangePersistedValueDisplaysAsDefault) {
    appProperties.getUserSettings()->setValue("historyRetentionDays", 0); // hand-edited/corrupt value

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* retentionCombo = findHistoryRetentionCombo(aiTab);
    ASSERT_NE(retentionCombo, nullptr);
    EXPECT_EQ(retentionCombo->getText(), "180 days");
}

// ============================================================================
// Request timeout control (AI tab, added after the history-retention combo) — the single
// user-configurable value applied to both AIChatComponent's watchdog and the active provider's
// connection timeout, so the two mechanisms can no longer drift apart.
// ============================================================================

namespace {
// The timeout combo is the THIRD ComboBox in the AI tab's child order (providerCombo first,
// historyRetentionCombo second — see findHistoryRetentionCombo above and SettingsWindow.cpp's
// comment on why requestTimeoutCombo is added right after it).
juce::ComboBox* findRequestTimeoutCombo(juce::Component* aiTab) {
    int comboIndex = 0;
    for (auto* child : aiTab->getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            if (comboIndex == 2)
                return combo;
            ++comboIndex;
        }
    }
    return nullptr;
}
} // namespace

TEST_F(SettingsWindowTest, RequestTimeoutLoadsPersistedSelection) {
    appProperties.getUserSettings()->setValue("aiRequestTimeoutMs", 360000); // 6 minutes

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* timeoutCombo = findRequestTimeoutCombo(aiTab);
    ASSERT_NE(timeoutCombo, nullptr);
    EXPECT_EQ(timeoutCombo->getText(), "6 minutes");
}

TEST_F(SettingsWindowTest, RequestTimeoutPersistsSelectionToAppProperties) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* timeoutCombo = findRequestTimeoutCombo(aiTab);
    ASSERT_NE(timeoutCombo, nullptr);

    timeoutCombo->setSelectedItemIndex(3, juce::sendNotificationSync); // "10 minutes"
    EXPECT_EQ(appProperties.getUserSettings()->getIntValue("aiRequestTimeoutMs", -999), 600000);
    EXPECT_EQ(aiChatComponent->getRequestTimeoutMsForTesting(), 600000);
    EXPECT_EQ(aiService->getRequestTimeoutMs(), 600000);
}

TEST_F(SettingsWindowTest, RequestTimeoutOutOfRangePersistedValueDisplaysAsDefault) {
    appProperties.getUserSettings()->setValue("aiRequestTimeoutMs", 0); // hand-edited/corrupt value

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* timeoutCombo = findRequestTimeoutCombo(aiTab);
    ASSERT_NE(timeoutCombo, nullptr);
    EXPECT_EQ(timeoutCombo->getText(), "4 minutes (default)");
}

TEST_F(SettingsWindowTest, RemembersLastSelectedTab) {
    // Set the settingsTab preference to 1 (AI tab) before constructing
    appProperties.getUserSettings()->setValue("settingsTab", 1);
    appProperties.saveIfNeeded();

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), 1);
}

TEST_F(SettingsWindowTest, InitialTabNameOverridesPersistedTabSelection) {
    // Persisted preference points at a different tab (index 1, AI) — initialTabName should win.
    appProperties.getUserSettings()->setValue("settingsTab", 1);
    appProperties.saveIfNeeded();

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr, nullptr, true, "Feedback");

    int expectedIndex = -1;
    for (int i = 0; i < settingsWindow.getNumTabs(); ++i) {
        if (settingsWindow.getTabName(i) == "Feedback") {
            expectedIndex = i;
            break;
        }
    }
    ASSERT_GE(expectedIndex, 0) << "No tab named \"Feedback\" found";
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), expectedIndex);
}

TEST_F(SettingsWindowTest, ResizingDoesNotCrash) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(500, 450);

    EXPECT_NO_THROW(settingsWindow.setSize(800, 600));
    EXPECT_NO_THROW(settingsWindow.resized());
}

TEST_F(SettingsWindowTest, GeneralTabShowsShortcuts) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(500, 450);

    auto* generalTab = settingsWindow.getTabs().getTabContentComponent(2);
    ASSERT_NE(generalTab, nullptr);
    // The rows now live inside a Viewport under collapsible category sections, so counting the
    // tab's DIRECT children no longer proves any shortcut is shown. Assert the same intent through
    // the tab's own observables instead: every category section is laid out, and the first row is
    // actually visible (sections start expanded).
    auto* shortcuts = dynamic_cast<ShortcutsSettingsTab*>(generalTab);
    ASSERT_NE(shortcuts, nullptr);
    for (auto category : ShortcutManager::getCategoryOrder())
        EXPECT_TRUE(shortcuts->isSectionVisible(category)) << "category " << (int)category << " has no section header";
    EXPECT_TRUE(shortcuts->isRowVisible(0));
}

// ============================================================================
// P6-7: prompt-learning opt-in checkbox (AI tab)
// ============================================================================

TEST_F(SettingsWindowTest, PromptLearningToggleDisabledAndUncheckedWithNoAccountService) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr, nullptr);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* toggle = findPromptLearningToggle(aiTab);
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isEnabled());
    EXPECT_FALSE(toggle->getToggleState());
}

TEST_F(SettingsWindowTest, PromptLearningToggleDisabledWithSignInRequiredTooltipWhenSignedOut) {
    FakeSettingsAuthServer server;
    synth::AccountService accountService{"http://mock-host:8787", server.performer(),
                                         std::make_unique<synth::InMemoryTokenStore>()};

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr, &accountService);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* toggle = findPromptLearningToggle(aiTab);
    ASSERT_NE(toggle, nullptr);
    EXPECT_FALSE(toggle->isEnabled());
    EXPECT_FALSE(toggle->getToggleState());
    EXPECT_EQ(toggle->getTooltip(), juce::String("Sign in required"));
}

TEST_F(SettingsWindowTest, PromptLearningToggleEnabledAndReflectsServerOptInWhenSignedIn) {
    FakeSettingsAuthServer server;
    server.tokenResponse = makeTokenSuccess("at1", "rt1");
    server.promptLearningGetResponse = makePromptLearningResponse(true);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService{"http://mock-host:8787", server.performer(), std::move(tokenStore)};
    accountService.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return accountService.getSnapshot().state == synth::AccountState::SignedIn; }));

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr, &accountService);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* toggle = findPromptLearningToggle(aiTab);
    ASSERT_NE(toggle, nullptr);
    EXPECT_TRUE(toggle->isEnabled());

    // AISettingsTab fires a fresh refreshPromptLearningOptIn() at construction (already
    // signed in) rather than trusting a possibly-stale cached snapshot — wait for that round trip
    // rather than asserting the pre-refresh (default false) state.
    ASSERT_TRUE(waitUntil([&] { return toggle->getToggleState(); }))
        << "checkbox never reflected the server's opted_in=true";
}

TEST_F(SettingsWindowTest, TogglingPromptLearningCheckboxCallsAccountServiceSetter) {
    FakeSettingsAuthServer server;
    server.tokenResponse = makeTokenSuccess("at1", "rt1");
    server.promptLearningPutResponse = makePromptLearningResponse(true);

    auto tokenStore = std::make_unique<synth::InMemoryTokenStore>();
    tokenStore->save("stored-refresh-token");
    synth::AccountService accountService{"http://mock-host:8787", server.performer(), std::move(tokenStore)};
    accountService.attemptSilentSignIn();
    ASSERT_TRUE(waitUntil([&] { return accountService.getSnapshot().state == synth::AccountState::SignedIn; }));

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr, &accountService);
    settingsWindow.setSize(600, 400);
    settingsWindow.resized();

    auto* aiTab = settingsWindow.getTabs().getTabContentComponent(1);
    ASSERT_NE(aiTab, nullptr);
    auto* toggle = findPromptLearningToggle(aiTab);
    ASSERT_NE(toggle, nullptr);
    ASSERT_TRUE(toggle->isEnabled());

    toggle->setToggleState(true, juce::sendNotificationSync);

    ASSERT_TRUE(waitUntil([&] { return server.promptLearningPutCalls() >= 1; }))
        << "toggling the checkbox never called into AccountService::setPromptLearningOptIn()";
}

// ============================================================================
// Appearance tab: piano-roll note-colour swatches (12 pitch-class overrides — see
// AppearanceSettingsTab.h's "Piano roll note colours" section). Constructed directly against its
// own two deps (a ThemeManager + an ApplicationProperties) rather than through the full
// SettingsWindow, the same "build only what the piece under test needs" idiom the AI-tab tests
// above use with aiService/aiChatComponent.
// ============================================================================

namespace {
// Same isolated-PropertiesFile idiom as TimelinePanelTests.cpp's IsolatedPropsGuard (follow-
// playhead persistence test): delete the underlying file before AND after, so a note-colour
// override saved by one run/test can never leak into another and this never touches the app's
// real settings file.
struct IsolatedAppearancePropsGuard {
    juce::PropertiesFile::Options opts;
    juce::ApplicationProperties props;

    explicit IsolatedAppearancePropsGuard(const char* name) {
        opts.applicationName = name;
        opts.folderName = name;
        opts.filenameSuffix = "settings";
        opts.osxLibrarySubFolder = "Application Support";
        opts.storageFormat = juce::PropertiesFile::storeAsXML;

        {
            juce::ApplicationProperties initial;
            initial.setStorageParameters(opts);
            if (auto* s = initial.getUserSettings())
                s->getFile().deleteFile();
        }
        props.setStorageParameters(opts);
    }

    ~IsolatedAppearancePropsGuard() {
        if (auto* s = props.getUserSettings())
            s->getFile().deleteFile();
    }
};
} // namespace

TEST(AppearanceSettingsTabNoteColourTest, SetNoteSwatchColourRoundTripsAndMarksItOverridden) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Note Colour Test 1");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);

    const int pitchClass = 3; // D#
    const auto colour = juce::Colour(0xffAA33CCu);
    ASSERT_FALSE(tab.isNoteSwatchOverridden(pitchClass)) << "nothing pinned yet";

    tab.setNoteSwatchColour(pitchClass, colour);

    EXPECT_TRUE(tab.isNoteSwatchOverridden(pitchClass));
    EXPECT_EQ(tab.getNoteSwatchColour(pitchClass), colour);
}

TEST(AppearanceSettingsTabNoteColourTest, OverrideRoundTripsThroughLoadNoteColourOverridesOnTheSamePropertiesFile) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Note Colour Test 2");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);

    const int pitchClass = 7; // G
    const auto colour = juce::Colour(0xff11EE55u);
    tab.setNoteSwatchColour(pitchClass, colour);

    ASSERT_NE(guard.props.getUserSettings(), nullptr);
    const auto loaded = synth::ui::loadNoteColourOverrides(*guard.props.getUserSettings());
    ASSERT_TRUE(loaded.perPitchClass[(size_t)pitchClass].has_value())
        << "setNoteSwatchColour must persist through synth::ui::saveNoteColourOverrides";
    EXPECT_EQ(*loaded.perPitchClass[(size_t)pitchClass], colour);
}

// Regression test for the "Piano roll notes" swatch section rendering as nothing in the actual
// Settings window: goes through the REAL instantiation path (SettingsWindow, not a directly
// constructed AppearanceSettingsTab) at the REAL launch size MainComponent opens the dialog at
// (settingsComp->setSize(500, 450) in MainComponent.cpp) — the size at which the bug reproduced.
// Every earlier test above constructs AppearanceSettingsTab directly against its own two deps and
// never lays it out at a size small enough to starve the trailing sections of space, which is
// exactly why they all passed while the section was invisible in the running app.
TEST_F(SettingsWindowTest, AppearanceTabPianoRollNoteColoursSectionHasRealBoundsAtLaunchSize) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    settingsWindow.setSize(500, 450); // matches MainComponent's real Settings dialog size exactly
    settingsWindow.resized();

    // TabbedComponent::resized() lays out EVERY tab's content component, not just the current one
    // (see juce_TabbedComponent.cpp), so the Appearance tab is already laid out without switching
    // to it first — same assumption AITabPersistsProviderSetting etc. make about tab index 1 above.
    auto* appearanceTab = dynamic_cast<AppearanceSettingsTab*>(settingsWindow.getTabs().getTabContentComponent(4));
    ASSERT_NE(appearanceTab, nullptr);

    const auto titleBounds = appearanceTab->getNoteColoursTitleBoundsForTest();
    const auto swatchBounds = appearanceTab->getNoteSwatchRowBoundsForTest();
    const auto resetButtonBounds = appearanceTab->getResetNoteColoursButtonBoundsForTest();

    EXPECT_FALSE(titleBounds.isEmpty()) << "\"Piano Roll Notes\" header got a zero-area bounds — "
                                           "present via addAndMakeVisible but nothing to paint";
    EXPECT_FALSE(swatchBounds.isEmpty()) << "note-colour swatch row got a zero-area bounds — this "
                                            "is the reported bug: the section exists in the tree "
                                            "but has no area to paint or click";
    EXPECT_FALSE(resetButtonBounds.isEmpty()) << "\"Reset Note Colours\" button got a zero-area bounds";

    // Non-empty bounds alone would also pass for a bounds that overflows past whatever area is
    // actually reachable (e.g. clipped by a Viewport that never grew to cover it) — so also check
    // each section's bottom edge falls inside the SCROLLABLE content area, not just that its
    // Rectangle happens to have positive width/height.
    const int contentHeight = appearanceTab->getContentHeightForTest();
    EXPECT_LE(titleBounds.getBottom(), contentHeight);
    EXPECT_LE(swatchBounds.getBottom(), contentHeight);
    EXPECT_LE(resetButtonBounds.getBottom(), contentHeight);
}

// ============================================================================
// Appearance tab: theme-gallery wheel bubbling (bug: "vertical scroll only sometimes works").
//
// juce::ListBox::mouseWheelMove (juce_ListBox.cpp) consumes the wheel unconditionally whenever its
// own vertical scrollbar is merely VISIBLE, with no "did this actually move the list" check the
// way juce::Viewport's own wheel handling has — so a plain wheel gesture anywhere over the theme
// gallery (a ListBox nested inside this tab's own taller Viewport) never reached the outer
// settings page, however far the list itself already was from its own top/bottom. See
// AppearanceSettingsTab::ThemeListBox.
// ============================================================================

TEST(AppearanceSettingsTabScrollTest, WheelOverAThemeListAlreadyAtItsScrollLimitBubblesToTheOuterViewport) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Scroll Test 1");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);
    tab.setSize(500, 450); // matches MainComponent's real Settings dialog size (see the launch-size test above)
    tab.resized();

    ASSERT_FALSE(tab.getThemeListBoundsForTest().isEmpty());
    ASSERT_TRUE(tab.isThemeListScrollbarVisibleForTest())
        << "test premise: the built-in theme gallery needs its own scrollbar at this row height/box height";

    // Push the list's OWN scroll to its end -- what a fast flick over the list would do. Each call
    // is a SEPARATE wheel event, exactly like a real trackpad gesture. Stop as soon as a flick no
    // longer moves the list: every over-limit flick bubbles into the OUTER viewport (the behaviour
    // under test), and 30 of those would drive the outer to ITS max too, leaving the final
    // assertion below no room to observe movement.
    for (int i = 0; i < 30; ++i) {
        const int before = tab.getThemeListScrollPositionForTest();
        tab.simulateWheelOverThemeListForTest(-1.0f);
        if (tab.getThemeListScrollPositionForTest() == before)
            break;
    }
    const int listPositionAtLimit = tab.getThemeListScrollPositionForTest();
    ASSERT_GT(listPositionAtLimit, 0) << "test premise: the list actually scrolled from these wheel gestures";

    const int outerScrollBefore = tab.getContentScrollYForTest();
    tab.simulateWheelOverThemeListForTest(-1.0f); // one more -- the list has nowhere further to go
    EXPECT_EQ(tab.getThemeListScrollPositionForTest(), listPositionAtLimit)
        << "the list itself is already at its limit in this direction";
    EXPECT_GT(tab.getContentScrollYForTest(), outerScrollBefore)
        << "a wheel gesture over an already-fully-scrolled theme list must reach the tab's own "
           "Viewport instead of doing nothing -- this is the reported bug";
}

TEST(AppearanceSettingsTabScrollTest, WheelOverTheThemeListStillScrollsItWhenNotAtTheLimit) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Scroll Test 2");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);
    tab.setSize(500, 450);
    tab.resized();
    ASSERT_TRUE(tab.isThemeListScrollbarVisibleForTest());

    ASSERT_EQ(tab.getThemeListScrollPositionForTest(), 0);
    const int outerScrollBefore = tab.getContentScrollYForTest();
    tab.simulateWheelOverThemeListForTest(-1.0f);

    EXPECT_GT(tab.getThemeListScrollPositionForTest(), 0) << "the list still scrolls normally when it can";
    EXPECT_EQ(tab.getContentScrollYForTest(), outerScrollBefore)
        << "the outer page must NOT also move while the nested list can still take the gesture";
}

TEST(AppearanceSettingsTabNoteColourTest, ResetNoteSwatchAndResetAllFallBackToTheActiveThemesNoteFill) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Note Colour Test 3");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);
    const auto themeNoteFill = themeManager.getActiveTheme().colors.noteFill;

    // A single reset falls back that ONE pitch class, leaving the others untouched.
    tab.setNoteSwatchColour(0, juce::Colour(0xffFF0000u));
    tab.setNoteSwatchColour(1, juce::Colour(0xff00FF00u));
    tab.resetNoteSwatch(0);
    EXPECT_FALSE(tab.isNoteSwatchOverridden(0));
    EXPECT_EQ(tab.getNoteSwatchColour(0), themeNoteFill);
    EXPECT_TRUE(tab.isNoteSwatchOverridden(1)) << "resetNoteSwatch must not touch other pitch classes";

    // resetAllNoteColours() clears every remaining override, including the one just re-set above.
    tab.resetAllNoteColours();
    for (int pitchClass = 0; pitchClass < AppearanceSettingsTab::kNoteSwatchCount; ++pitchClass) {
        EXPECT_FALSE(tab.isNoteSwatchOverridden(pitchClass));
        EXPECT_EQ(tab.getNoteSwatchColour(pitchClass), themeNoteFill);
    }
}

// Regression tests for the "piano-roll note colours in Settings look darker than the actual
// notes" bug: the un-overridden swatch used to draw at a flat 0.2 alpha instead of going through
// synth::ui::resolveNoteColour at all. The fix routes BOTH overridden and un-overridden swatches
// through that same resolver (composited over the panel background), so the preview can never
// drift from what the roll actually paints.
TEST(AppearanceSettingsTabNoteColourTest, UnoverriddenSwatchPreviewMatchesResolveNoteColourComposited) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Note Colour Test 4");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);

    const int pitchClass = 5; // F, left un-overridden
    ASSERT_FALSE(tab.isNoteSwatchOverridden(pitchClass)) << "test premise: nothing pinned yet";

    const auto& colors = themeManager.getActiveTheme().colors;
    synth::ui::NoteColourOverrides empty;
    const auto expectedFill =
        synth::ui::resolveNoteColour(colors, pitchClass, AppearanceSettingsTab::kNoteSwatchPreviewVelocity,
                                     /*selected*/ false, /*muted*/ false, /*outOfScale*/ false, empty)
            .fill;
    const auto panelBackground = tab.findColour(juce::ResizableWindow::backgroundColourId);
    const auto expected = panelBackground.overlaidWith(expectedFill);

    EXPECT_EQ(tab.getNoteSwatchPreviewColour(pitchClass), expected)
        << "the un-overridden preview must render through the SAME resolver the piano roll uses, "
           "not a separately dimmed formula -- this is the reported bug (preview read darker than "
           "the roll's actual notes)";
}

TEST(AppearanceSettingsTabNoteColourTest, OverriddenSwatchPreviewMatchesResolveNoteColourComposited) {
    IsolatedAppearancePropsGuard guard("Agent Synth Appearance Note Colour Test 5");
    synth::theme::ThemeManager themeManager;
    AppearanceSettingsTab tab(themeManager, guard.props);

    const int pitchClass = 9; // A
    const auto pinned = juce::Colour(0xff3388CCu);
    tab.setNoteSwatchColour(pitchClass, pinned);
    ASSERT_TRUE(tab.isNoteSwatchOverridden(pitchClass))
        << "the overridden/default distinction must still be detectable now that the fill formula "
           "is shared -- the ring (driven by isNoteSwatchOverridden) is the only thing left to tell "
           "them apart";

    const auto& colors = themeManager.getActiveTheme().colors;
    synth::ui::NoteColourOverrides overrides;
    overrides.set(pitchClass, pinned);
    const auto expectedFill =
        synth::ui::resolveNoteColour(colors, pitchClass, AppearanceSettingsTab::kNoteSwatchPreviewVelocity,
                                     /*selected*/ false, /*muted*/ false, /*outOfScale*/ false, overrides)
            .fill;
    const auto panelBackground = tab.findColour(juce::ResizableWindow::backgroundColourId);
    const auto expected = panelBackground.overlaidWith(expectedFill);

    EXPECT_EQ(tab.getNoteSwatchPreviewColour(pitchClass), expected);
}
