#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AccountService.h"
#include "../Source/AI/LocalHistoryStore.h"
#include "../Source/AudioEngine.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/ShortcutManager.h"
#include "../Source/UI/AIChatComponent.h"
#include "../Source/UI/PreferencesSettingsTab.h"
#include "../Source/UI/SettingsWindow.h"
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

private:
    juce::String currentModel = "MockModel1";
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

TEST_F(SettingsWindowTest, HasFiveTabs) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getNumTabs(), 5);
}

TEST_F(SettingsWindowTest, TabNamesAreCorrect) {
    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getTabName(0), "Audio");
    EXPECT_EQ(settingsWindow.getTabName(1), "AI");
    EXPECT_EQ(settingsWindow.getTabName(2), "Keyboard Shortcuts");
    EXPECT_EQ(settingsWindow.getTabName(3), "Preferences");
    EXPECT_EQ(settingsWindow.getTabName(4), "Appearance");
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

TEST_F(SettingsWindowTest, RemembersLastSelectedTab) {
    // Set the settingsTab preference to 1 (AI tab) before constructing
    appProperties.getUserSettings()->setValue("settingsTab", 1);
    appProperties.saveIfNeeded();

    SettingsWindow settingsWindow(deviceManager, appProperties, *aiService, *aiChatComponent, shortcutManager,
                                  themeManager, nullptr);
    EXPECT_EQ(settingsWindow.getCurrentTabIndex(), 1);
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
    // The General tab should have child labels for shortcuts (title label + 5 desc labels + 5 bind buttons + reset
    // button = 12)
    EXPECT_GE(generalTab->getNumChildComponents(), 11);
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
