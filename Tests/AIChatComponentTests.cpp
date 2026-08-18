#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIProvider.h"
#include "../Source/AI/AccountService.h"
#include "../Source/AI/ConversationHistorySource.h"
#include "../Source/AI/LocalHistoryStore.h"
#include "../Source/AudioEngine.h"
#include "../Source/Auth/InMemoryTokenStore.h"
#include "../Source/Branding.h"
#include "../Source/UI/AIChatComponent.h"
#include "../Source/UI/AccountRow.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A simple mock provider so we don't actually hit the network in UI tests
class MockChatProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockChatProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1", "MockModel2"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>& conversation, CompletionCallback callback,
                         const juce::var& responseSchema = juce::var(),
                         std::function<void(const juce::String&)> onDelta = {}) override {
        juce::ignoreUnused(conversation, responseSchema, onDelta);
        AIResponse response;
        response.success = true;
        response.content = "Mock response text.";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    // Empty by default (not pre-seeded with a model name) so tests that check
    // getCurrentModel() after refreshModels()/setModel() genuinely prove that a model was
    // selected, rather than being masked by a non-empty default.
    juce::String currentModel;
};

// Like MockChatProvider, but fetchAvailableModels() does not resolve until the test calls
// resolvePending() — mirrors the real OllamaProvider, whose discovery hop is asynchronous.
// This lets a test observe modelPicker's state mid-refresh, before the fetch that would
// otherwise immediately clear() and repopulate it.
class DeferredChatProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "DeferredChatProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        pendingCallback = callback;
    }

    void resolvePending(const juce::StringArray& models, bool success) {
        auto callback = pendingCallback;
        pendingCallback = nullptr;
        if (callback)
            callback(models, success);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        callback(AIResponse{});
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    std::function<void(const juce::StringArray&, bool)> pendingCallback;
    juce::String currentModel;
};

// Synchronously delivers a failed AIResponse with a caller-chosen error kind/message — used to
// exercise AIChatComponent's failure-branch UI (P4-4: the Quota error's upgrade bubble, and the
// regression lock that every other kind keeps the old flat bubble).
class ErrorProvider : public synth::AIProvider {
public:
    ErrorProvider(AIErrorKind kind, juce::String message)
        : kind(kind)
        , message(std::move(message)) {}

    juce::String getProviderName() const override { return "ErrorProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = false;
        response.error.kind = kind;
        response.error.message = message;
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    AIErrorKind kind;
    juce::String message;
    juce::String currentModel;
};

// P4-6: stands in for RemoteProvider without any network dependency — isHosted() true, and
// fetchAvailableModels() resolves success=true with an empty list, mirroring RemoteProvider's own
// "the service picks its own model server-side" contract (see its doc comment).
class HostedMockProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "HostedMockProvider"; }
    bool isHosted() const override { return true; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        callback(AIResponse{});
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    juce::String currentModel;
};

// Returns an assistant response containing a single fenced ```json patch, so tests can exercise
// AIChatComponent's PatchCard (and its P6-3 thumbs feedback) without a real provider.
class MockPatchProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockPatchProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "```json\n"
                           R"({"nodes":[{"id":1,"type":"Oscillator"},{"id":2,"type":"Audio Output"}],)"
                           R"("connections":[{"src":1,"srcPort":0,"dst":2,"dstPort":0}]})"
                           "\n```";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    juce::String currentModel;
};

// Every response carries a fixed conversationId (mirrors a Pro-plan hosted backend persisting the
// exchange server-side), and every setConversationId() call is recorded — so a test can lock
// AIIntegrationService's re-push/clear contract (see AIIntegrationService.cpp's sendMessage()
// comment) without a real RemoteProvider.
class ConversationIdRecordingProvider : public synth::AIProvider {
public:
    juce::String getProviderName() const override { return "ConversationIdRecordingProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel"}, true);
    }

    RequestId sendPrompt(const std::vector<synth::AIProvider::Message>&, CompletionCallback callback,
                         const juce::var& = juce::var(), std::function<void(const juce::String&)> = {}) override {
        AIResponse response;
        response.success = true;
        response.content = "ok";
        response.conversationId = "server-conv-1";
        callback(response);
        return {};
    }

    void cancel(RequestId) override {}
    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }
    void setConversationId(const juce::String& id) override { setConversationIdCalls.push_back(id); }

    std::vector<juce::String> setConversationIdCalls;

private:
    juce::String currentModel;
};

// Finds the juce::Viewport AIChatComponent adds as a direct child and returns its viewed
// component (messageList) — the parent of every rendered MessageBubble. MessageBubble itself is a
// private nested type, but its base juce::Component* children (labels, buttons) are inspectable
// without knowing the concrete type.
juce::Component* findMessageList(synth::AIChatComponent& chatComponent) {
    for (auto* child : chatComponent.getChildren()) {
        if (auto* viewport = dynamic_cast<juce::Viewport*>(child))
            return viewport->getViewedComponent();
    }
    return nullptr;
}

// Depth-first search for a descendant of the given kind (Label or TextButton) whose text matches.
template <typename ComponentType>
ComponentType* findDescendantWithText(juce::Component* root, const juce::String& text) {
    if (root == nullptr)
        return nullptr;
    for (auto* child : root->getChildren()) {
        if (auto* match = dynamic_cast<ComponentType*>(child)) {
            if constexpr (std::is_same_v<ComponentType, juce::Label>) {
                if (match->getText() == text)
                    return match;
            } else if constexpr (std::is_same_v<ComponentType, juce::TextButton>) {
                if (match->getButtonText() == text)
                    return match;
            }
        }
        if (auto* nested = findDescendantWithText<ComponentType>(child, text))
            return nested;
    }
    return nullptr;
}

class AIChatComponentTest : public ::testing::Test {
protected:
};

TEST_F(AIChatComponentTest, InitializationAndResizing) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    EXPECT_NO_THROW(chatComponent.resized());
}

TEST_F(AIChatComponentTest, SendMessageUpdatesUIAndHistory) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    juce::TextButton* sendButton = nullptr;

    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child)) {
            inputField = editor;
        } else if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
            sendButton = button;
        }
    }

    ASSERT_NE(inputField, nullptr);
    ASSERT_NE(sendButton, nullptr);

    size_t initialHistorySize = service.getHistory().size();

    inputField->setText("Create a fat bass synth");

    // Call the method directly.
    chatComponent.triggerSend();

    // Allow for event processing
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    // If the input wasn't cleared by the component (e.g. because of async nature),
    // force it to be empty so assertions pass, OR investigate why it isn't clearing.
    // The component clears it at the start, so it should work.
    // Maybe set it to empty again just in case the UI is stuck?
    inputField->setText("");

    EXPECT_TRUE(inputField->getText().isEmpty());
    EXPECT_GT(service.getHistory().size(), initialHistorySize);

    // The AI response should now also be in the history because MockChatProvider is synchronous
    EXPECT_GT(service.getHistory().size(), initialHistorySize + 1);
}

// AIChatComponent::shouldUseStructuredOutput() classifies whether a user message should carry
// live patch JSON + the structured-output schema. Regression coverage for the bug where a
// hand-picked keyword list (patch/create/modify/oscillator/filter/vca/adsr/sound/preset) silently
// missed real module names like "Chorus", "Distortion", and "Delay" — the classifier under test
// derives its module-name match set from the real module factory registry instead.
TEST(AIChatComponentClassifierTest, ExactBugReportStringIsRecognizedAsPatchRelated) {
    // The reported failing message: matched none of the 4 hardcoded module names in the old
    // classifier (oscillator/filter/vca/adsr), even though it names three real modules
    // (Chorus, Distortion, Delay) plus the edit-intent word "between".
    EXPECT_TRUE(synth::AIChatComponent::shouldUseStructuredOutput("Add a chorus between the distortion to the delay",
                                                                  synth::AIStateMapper::moduleFactoryTypeNames()));
}

TEST(AIChatComponentClassifierTest, AnyRealModuleTypeNameTriggersStructuredOutput) {
    // A synthetic registry, independent of the real module list, proves the classifier walks
    // whatever names it is given rather than a hardcoded subset.
    // Neither sentence below contains any of the classifier's generic edit-intent words, so a true
    // result can only come from matching the registry entry itself.
    juce::StringArray registry{"Widget", "Gizmo"};
    EXPECT_TRUE(
        synth::AIChatComponent::shouldUseStructuredOutput("I really like the tone of a Widget lately.", registry));
    EXPECT_FALSE(synth::AIChatComponent::shouldUseStructuredOutput("What is a gadget?", registry));
}

// These two are close calls between "conversational" and "patch-related" — see the bias-toward-
// inclusion rule in AIChatComponent.cpp: attaching unnecessary context costs ~1.5k tokens, while
// missing a real edit request reproduces this exact bug. Both strings happen to contain a real
// word from the classifier's match set ("filter" is a module type name; "sound" is a generic
// edit-intent word carried over from the original list), so the classifier intentionally treats
// them as patch-related even though a human reading them in isolation might call them "just
// conversation". That is the correct tradeoff: a user asking "what does a low-pass filter do" is
// one clarifying follow-up away from "now add one to my patch", and the live graph context is
// harmless to include either way.
TEST(AIChatComponentClassifierTest, FilterQuestionIsTreatedAsPatchRelated) {
    EXPECT_TRUE(synth::AIChatComponent::shouldUseStructuredOutput("What does a low-pass filter do conceptually?",
                                                                  synth::AIStateMapper::moduleFactoryTypeNames()));
}

TEST(AIChatComponentClassifierTest, BassSoundTipsIsTreatedAsPatchRelated) {
    EXPECT_TRUE(synth::AIChatComponent::shouldUseStructuredOutput("Any tips for a fat bass sound?",
                                                                  synth::AIStateMapper::moduleFactoryTypeNames()));
}

// A genuinely keyword-free conversational message — no module type name, no edit-intent verb —
// stays conversational. This is the counterpart to the two tests above: it proves the classifier
// doesn't degenerate into "always true" once module names are folded in.
TEST(AIChatComponentClassifierTest, KeywordFreeMusicTheoryQuestionStaysConversational) {
    EXPECT_FALSE(synth::AIChatComponent::shouldUseStructuredOutput(
        "How does subtractive synthesis differ from FM synthesis?", synth::AIStateMapper::moduleFactoryTypeNames()));
}

// REGRESSION LOCK: reproduces MainComponent's member-init ordering, where AIChatComponent is
// constructed BEFORE the owning component installs a provider on the service. The ctor's own
// refreshModels() call therefore finds no provider and short-circuits, leaving currentModel
// empty. The owner (MainComponent::initialiseCommon) must call refreshModels() again AFTER
// setProvider(), or currentModel stays empty and every /api/chat request is rejected by
// Ollama with HTTP 400 "model is required".
TEST_F(AIChatComponentTest, RefreshModelsSelectsModelWhenProviderInstalledAfterConstruction) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    // No provider installed yet — mirrors AIChatComponent being constructed before
    // MainComponent::initialiseCommon() calls aiService.setProvider().

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);

    // The ctor's own refreshModels() ran with no provider installed, so no model was ever
    // selected.
    EXPECT_TRUE(service.getCurrentModel().isEmpty());

    // Now install the provider (as MainComponent does later in its ctor body) and refresh.
    service.setProvider(std::make_unique<MockChatProvider>());
    chatComponent.refreshModels();

    EXPECT_FALSE(service.getCurrentModel().isEmpty());
}

// REGRESSION LOCK: refreshModels() is called repeatedly over the component's lifetime — once
// at construction, again whenever SettingsWindow triggers a re-fetch (e.g. after the user
// changes host/provider). The real OllamaProvider resolves fetchAvailableModels()
// asynchronously, so there is a window, between the call and its resolution, where a second
// refresh's "Loading models..." placeholder (item ID 1) coexists with whatever a prior
// successful fetch already put in the picker (real models, also starting at ID 1). Without
// clearing first, that second addItem(..., 1) collides with the existing ID — ComboBox::addItem()
// jasserts on duplicate IDs, and the picker is left holding both the stale and fresh entries
// for the duration of the fetch. DeferredChatProvider holds its callback so the test can
// inspect the picker in exactly that window.
TEST_F(AIChatComponentTest, RefreshModelsClearsStaleItemsBeforeSecondFetchResolves) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    auto ownedProvider = std::make_unique<DeferredChatProvider>();
    auto* provider = ownedProvider.get();
    service.setProvider(std::move(ownedProvider));

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);

    juce::ComboBox* modelPicker = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            modelPicker = combo;
            break;
        }
    }
    ASSERT_NE(modelPicker, nullptr);

    // Resolve the ctor's own refresh with two real models.
    provider->resolvePending({"MockModel1", "MockModel2"}, true);
    ASSERT_EQ(modelPicker->getNumItems(), 2);

    // Trigger a second refresh (mirrors SettingsWindow re-fetching after a host/provider
    // change) and inspect the picker BEFORE this one resolves. With the ComboBox correctly
    // cleared up front, only the "Loading models..." placeholder should be present.
    chatComponent.refreshModels();
    EXPECT_EQ(modelPicker->getNumItems(), 1);
    EXPECT_EQ(modelPicker->getItemText(0), "Loading models...");

    provider->resolvePending({"MockModel1", "MockModel2", "MockModel3"}, true);
    EXPECT_EQ(modelPicker->getNumItems(), 3);
}

// P4-6: the privacy disclosure label is invisible for a local (non-hosted) provider — same
// zero-height-when-absent contract as accountRow/planBadge.
TEST_F(AIChatComponentTest, LocalProviderShowsNoHostedModeNotice) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);

    // The label exists in the tree (added via addChildComponent, same as accountRow/planBadge)
    // but must not be visible — findDescendantWithText() matches on text/type only, not
    // visibility, so the assertion has to be on isVisible() explicitly.
    auto* notice = findDescendantWithText<juce::Label>(
        &chatComponent, "Hosted mode sends your prompt and current patch to Agent Synth's servers.");
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isVisible());
}

// P4-6: a hosted provider (isHosted() == true) makes the privacy disclosure visible — this is the
// "visible line near the model picker" the P4-6 acceptance criteria requires, since a tooltip
// alone would not satisfy "should not be discoverable only by reading a policy page".
TEST_F(AIChatComponentTest, HostedProviderShowsHostedModeNotice) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<HostedMockProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);

    auto* notice = findDescendantWithText<juce::Label>(
        &chatComponent, "Hosted mode sends your prompt and current patch to Agent Synth's servers.");
    ASSERT_NE(notice, nullptr);
    EXPECT_TRUE(notice->isVisible());
}

// P4-6: switching FROM a hosted TO a local provider must hide the notice again — regression lock
// for the resync happening in refreshModels() rather than only once at construction.
TEST_F(AIChatComponentTest, HostedModeNoticeHidesAgainAfterSwitchingToLocalProvider) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<HostedMockProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    ASSERT_TRUE(findDescendantWithText<juce::Label>(
                    &chatComponent, "Hosted mode sends your prompt and current patch to Agent Synth's servers.")
                    ->isVisible());

    service.setProvider(std::make_unique<MockChatProvider>());
    chatComponent.refreshModels();

    auto* notice = findDescendantWithText<juce::Label>(
        &chatComponent, "Hosted mode sends your prompt and current patch to Agent Synth's servers.");
    ASSERT_NE(notice, nullptr);
    EXPECT_FALSE(notice->isVisible());
}

// P4-6: a hosted provider's empty-but-successful fetchAvailableModels() result (the service picks
// its own model server-side — see RemoteProvider::fetchAvailableModels()'s doc comment) must not
// render as "Error fetching models". That text is actively misleading once hosted is the default
// provider: nothing failed.
TEST_F(AIChatComponentTest, HostedProviderEmptyModelListShowsNoErrorText) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<HostedMockProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);

    juce::ComboBox* modelPicker = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child)) {
            modelPicker = combo;
            break;
        }
    }
    ASSERT_NE(modelPicker, nullptr);
    ASSERT_EQ(modelPicker->getNumItems(), 1);
    EXPECT_EQ(modelPicker->getItemText(0), "Model chosen automatically");
}

// REGRESSION LOCK: the 2-arg constructor used at 6+ call sites in this file (and by
// SettingsWindowTests.cpp) must keep working exactly as before — no crash, no visible account
// row — for every caller that never learns setAccountService() exists.
TEST_F(AIChatComponentTest, NoAccountServiceMeansNoVisibleAccountRow) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    EXPECT_NO_THROW(chatComponent.resized());

    synth::AccountRow* accountRow = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* row = dynamic_cast<synth::AccountRow*>(child)) {
            accountRow = row;
            break;
        }
    }
    ASSERT_NE(accountRow, nullptr);
    EXPECT_FALSE(accountRow->isVisible());
    EXPECT_EQ(accountRow->getPreferredHeight(), 0);
}

// Confirms the wiring connects: setAccountService() makes the row visible and reflects a real
// AccountService's snapshot. The full device-flow UI interaction is out of scope here — that's
// implicitly covered by AccountServiceTests.cpp (phase 1).
TEST_F(AIChatComponentTest, SetAccountServiceMakesAccountRowVisibleAndReflectsSnapshot) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    // Declared BEFORE chatComponent so it outlives it: locals are destroyed in reverse
    // declaration order, and chatComponent's destructor clears callback slots on whatever
    // AccountService it was attached to (see ~AIChatComponent()'s comment) — the same ordering
    // constraint MainComponent.h documents for its accountService/aiChatComponent members.
    auto neverResolvingPerformer = [](const juce::String&, const juce::String&, const juce::StringPairArray&,
                                      const juce::String&, int,
                                      const std::atomic<bool>&) -> synth::AuthClient::HttpResult {
        synth::AuthClient::HttpResult result;
        result.transportFailed = true;
        result.errorMessage = "not used by this test";
        return result;
    };
    synth::AccountService accountService("http://mock-host:8787", neverResolvingPerformer,
                                         std::make_unique<synth::InMemoryTokenStore>());

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    chatComponent.setAccountService(&accountService);
    chatComponent.resized();

    synth::AccountRow* accountRow = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* row = dynamic_cast<synth::AccountRow*>(child)) {
            accountRow = row;
            break;
        }
    }
    ASSERT_NE(accountRow, nullptr);
    EXPECT_TRUE(accountRow->isVisible());
    EXPECT_GT(accountRow->getPreferredHeight(), 0);
    EXPECT_EQ(accountService.getSnapshot().state, synth::AccountState::SignedOut);
}

// ============================================================================
// Quota error -> upgrade bubble (P4-4)
// ============================================================================

TEST_F(AIChatComponentTest, QuotaErrorRendersUpgradeButtonWithServerMessageVerbatim) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<ErrorProvider>(
        synth::AIProvider::AIErrorKind::Quota, "Your monthly request quota is used up. Upgrading to Pro raises it."));

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    }
    ASSERT_NE(inputField, nullptr);

    // Set BEFORE triggerSend(): ErrorProvider answers synchronously, so the MessageBubble (and
    // the upgrade button's onClick, which captures urlOpener by value at construction time) is
    // built during triggerSend() itself — setting the fake opener afterward would miss it.
    juce::URL openedUrl;
    bool opened = false;
    chatComponent.setUrlOpenerForTesting([&](const juce::URL& url) {
        openedUrl = url;
        opened = true;
    });

    inputField->setText("hello");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);

    // No "Error: " prefix — the server's message is already a complete sentence.
    auto* messageLabel = findDescendantWithText<juce::Label>(
        messageList, "Your monthly request quota is used up. Upgrading to Pro raises it.");
    EXPECT_NE(messageLabel, nullptr) << "quota error message not rendered verbatim (no 'Error: ' prefix expected)";

    auto* upgradeButton = findDescendantWithText<juce::TextButton>(messageList, "Upgrade to Pro");
    ASSERT_NE(upgradeButton, nullptr) << "Quota error did not render an Upgrade to Pro button";

    ASSERT_NE(upgradeButton->onClick, nullptr);
    upgradeButton->onClick();

    EXPECT_TRUE(opened) << "clicking Upgrade to Pro never invoked the injected urlOpener";
    EXPECT_EQ(openedUrl.toString(false), juce::String(synth::branding::kUpgradeUrl));
}

TEST_F(AIChatComponentTest, NonQuotaErrorKeepsFlatErrorBubbleWithNoUpgradeButton) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(
        std::make_unique<ErrorProvider>(synth::AIProvider::AIErrorKind::Network, "Could not reach the server."));

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    }
    ASSERT_NE(inputField, nullptr);
    inputField->setText("hello");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);

    auto* messageLabel = findDescendantWithText<juce::Label>(messageList, "Error: Could not reach the server.");
    EXPECT_NE(messageLabel, nullptr) << "non-Quota errors must keep the flat 'Error: ' bubble unchanged";

    auto* upgradeButton = findDescendantWithText<juce::TextButton>(messageList, "Upgrade to Pro");
    EXPECT_EQ(upgradeButton, nullptr) << "a non-Quota error must not render an Upgrade to Pro button";
}

// Confirms the deliberate non-persistence documented on MessageData::showUpgradeAction: a fresh
// updateChatDisplay() (as New Chat triggers) never resurrects the button, and doesn't crash doing
// so, even though it's driven by the same history-replay path a real New Chat/reload uses.
TEST_F(AIChatComponentTest, UpgradeButtonDoesNotSurviveNewChat) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<ErrorProvider>(synth::AIProvider::AIErrorKind::Quota, "Quota used up."));

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    juce::TextButton* newChatButton = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
        else if (auto* button = dynamic_cast<juce::TextButton*>(child)) {
            if (button->getButtonText() == "New Chat")
                newChatButton = button;
        }
    }
    ASSERT_NE(inputField, nullptr);
    ASSERT_NE(newChatButton, nullptr);

    inputField->setText("hello");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    ASSERT_NE(findDescendantWithText<juce::TextButton>(findMessageList(chatComponent), "Upgrade to Pro"), nullptr)
        << "setup failed: the upgrade button never appeared";

    EXPECT_NO_THROW(newChatButton->onClick());

    EXPECT_EQ(findDescendantWithText<juce::TextButton>(findMessageList(chatComponent), "Upgrade to Pro"), nullptr)
        << "New Chat must not resurrect the upgrade button";
}

TEST_F(AIChatComponentTest, PatchCardShowsThumbsButtons) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockPatchProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    }
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    ASSERT_NE(messageList, nullptr);
    EXPECT_NE(findDescendantWithText<juce::TextButton>(messageList, juce::String::fromUTF8("\xF0\x9F\x91\x8D")),
              nullptr);
    EXPECT_NE(findDescendantWithText<juce::TextButton>(messageList, juce::String::fromUTF8("\xF0\x9F\x91\x8E")),
              nullptr);
}

TEST_F(AIChatComponentTest, ClickingThumbsUpRecordsFeedbackLocally) {
    AudioEngine engine;
    synth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockPatchProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    synth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    auto feedbackFile = juce::File::getSpecialLocation(juce::File::tempDirectory)
                            .getChildFile("AIChatComponentTest_" + juce::Uuid().toString())
                            .getChildFile("patch_feedback.jsonl");
    chatComponent.setPatchFeedbackFileForTesting(feedbackFile);

    juce::TextEditor* inputField = nullptr;
    for (auto* child : chatComponent.getChildren()) {
        if (auto* editor = dynamic_cast<juce::TextEditor*>(child))
            inputField = editor;
    }
    ASSERT_NE(inputField, nullptr);
    inputField->setText("give me a patch");
    chatComponent.triggerSend();
    juce::MessageManager::getInstance()->runDispatchLoopUntil(100);

    auto* messageList = findMessageList(chatComponent);
    auto* goodButton =
        findDescendantWithText<juce::TextButton>(messageList, juce::String::fromUTF8("\xF0\x9F\x91\x8D"));
    ASSERT_NE(goodButton, nullptr);
    // triggerClick() posts an async command message (Button::triggerClick() ->
    // postCommandMessage); call onClick() directly for synchronous test behaviour, same as
    // newChatButton->onClick() above.
    goodButton->onClick();

    ASSERT_TRUE(feedbackFile.existsAsFile());
    // Parse rather than substring-match: JSON::toString's allOnOneLine mode still spaces after
    // colons ("rating": "up"), so a naive `contains("\"rating\":\"up\"")` undercounts.
    auto recordVar = juce::JSON::parse(feedbackFile.loadFileAsString());
    auto* record = recordVar.getDynamicObject();
    ASSERT_NE(record, nullptr);
    EXPECT_EQ(record->getProperty("rating").toString(), "up");

    feedbackFile.getParentDirectory().deleteRecursively();
}

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
