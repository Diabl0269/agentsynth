#include "../Source/AI/AIIntegrationService.h"
#include "../Source/AI/AIProvider.h"
#include "../Source/AudioEngine.h"
#include "../Source/UI/AIChatComponent.h"
#include <gtest/gtest.h>
#include <juce_gui_basics/juce_gui_basics.h>

// A simple mock provider so we don't actually hit the network in UI tests
class MockChatProvider : public gsynth::AIProvider {
public:
    juce::String getProviderName() const override { return "MockChatProvider"; }

    void fetchAvailableModels(std::function<void(const juce::StringArray&, bool)> callback) override {
        callback({"MockModel1", "MockModel2"}, true);
    }

    void sendPrompt(const std::vector<gsynth::AIProvider::Message>& conversation, CompletionCallback callback,
                    const juce::var& responseSchema = juce::var()) override {
        callback("Mock response text.", true);
    }

    void setModel(const juce::String& name) override { currentModel = name; }
    juce::String getCurrentModel() const override { return currentModel; }

private:
    juce::String currentModel = "MockModel1";
};

class AIChatComponentTest : public ::testing::Test {
protected:
};

TEST_F(AIChatComponentTest, InitializationAndResizing) {
    AudioEngine engine;
    gsynth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    gsynth::AIChatComponent chatComponent(service, props);
    chatComponent.setSize(400, 600);

    EXPECT_NO_THROW(chatComponent.resized());
}

TEST_F(AIChatComponentTest, SendMessageUpdatesUIAndHistory) {
    AudioEngine engine;
    gsynth::AIIntegrationService service(engine.getGraph());
    service.setProvider(std::make_unique<MockChatProvider>());

    juce::ApplicationProperties props;
    juce::PropertiesFile::Options options;
    options.applicationName = "Test";
    options.filenameSuffix = "test";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    props.setStorageParameters(options);

    gsynth::AIChatComponent chatComponent(service, props);
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

    if (sendButton->onClick) {
        sendButton->onClick();
    }

    EXPECT_TRUE(inputField->getText().isEmpty());
    EXPECT_GT(service.getHistory().size(), initialHistorySize);

    // The AI response should now also be in the history because MockChatProvider is synchronous
    EXPECT_GT(service.getHistory().size(), initialHistorySize + 1);
}
