#pragma once

#include "../AI/AIIntegrationService.h"
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace gsynth {

/**
 * @class AIChatComponent
 * @brief Enhanced chat interface with collapsible patch cards and loading indicators.
 */
class AIChatComponent
    : public juce::Component
    , private juce::TextEditor::Listener
    , public juce::Timer
#ifndef NDEBUG
    , private juce::Logger
#endif
{
public:
    AIChatComponent(AIIntegrationService& service, juce::ApplicationProperties& props);
    ~AIChatComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    void refreshModels();

private:
    void timerCallback() override;
    class MessageBubble;
    class PatchCard;

    AIIntegrationService& aiService;
    juce::ApplicationProperties& appProperties;
    bool isWaitingForResponse = false;

    juce::Viewport viewport;
    juce::Component messageList;
    juce::TextEditor inputField;
    juce::TextButton sendButton;
    juce::TextButton newChatButton;
    juce::ComboBox modelPicker;

    void sendButtonClicked();
    void updateChatDisplay();
    void scrollToBottom();

    struct MessageData {
        juce::String role;
        juce::String text;
        juce::String jsonPatch;
        bool isExpanded = false;
    };
    std::vector<MessageData> messages;

#ifndef NDEBUG
    juce::TextEditor debugConsole;
    juce::TextButton toggleDebugButton;
    bool debugConsoleVisible = false;
    void logMessage(const juce::String& message) override;

public:
    void appendDebugLog(const juce::String& msg);

private:
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatComponent)
};

} // namespace gsynth
