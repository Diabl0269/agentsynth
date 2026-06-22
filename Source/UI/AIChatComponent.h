#pragma once

#include "../AI/AIIntegrationService.h"
#include "UIAnimation.h"
#include <atomic>
#include <juce_animation/juce_animation.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace gsynth {

/**
 * @class AIChatComponent
 * @brief Enhanced chat interface with collapsible patch cards and loading indicators.
 *
 * Phase 5 additions:
 *  - Cancel button: visible only while waiting; aborts in-flight request.
 *  - Animated pulse indicator: a small dot that throbs while waiting, stops on
 *    completion or cancel.  Confined to the input row — no per-tick global repaint.
 *  - Tooltips on all interactive controls.
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
    void sendButtonClicked();
    void triggerSend() { sendButtonClicked(); }

    // Testing hook: directly invokes the cancel action synchronously (mirrors triggerSend).
    // Use in headless tests instead of cancelButton->triggerClick(), which posts async.
    void simulateCancelClick() {
        cancelRequest();
        messages.push_back({"assistant", "Cancelled.", ""});
        updateChatDisplay();
    }

    // Testing hook: returns true if the cancel button is currently visible.
    bool isCancelVisible() const { return cancelButton.isVisible(); }

    // Testing hook: returns the current isWaitingForResponse flag.
    bool isWaiting() const { return isWaitingForResponse; }

private:
    void timerCallback() override;

    // Stops the in-flight request and resets all waiting state.
    // Called both by the cancel button and by the timeout path.
    void cancelRequest();

    class MessageBubble;
    class PatchCard;

    // ---- Spinner component -----------------------------------------------
    // A tiny 8×8 dot that pulses (alpha 0.3→1.0→0.3) via AnimationDriver.
    // Confined to a fixed region in the input row.  Only animates while
    // isWaitingForResponse is true.  ABSOLUTELY NO writeToLog/DBG inside paint().
    class SpinnerDot : public juce::Component {
    public:
        SpinnerDot() = default;

        void paint(juce::Graphics& g) override {
            g.setColour(juce::Colours::lightblue.withAlpha(currentAlpha));
            g.fillEllipse(getLocalBounds().toFloat().reduced(1.0f));
        }

        // Call once to start pulsing.  Caller must hold vblankUpdater alive for the
        // entire pulsing lifetime.  The pointer is stored so the recursive onComplete
        // callback can restart without a dangling reference to the function parameter.
        void startPulse(juce::VBlankAnimatorUpdater& updater) {
            updaterPtr = &updater;
            pulseAnim.start(
                updater,
                600.0,                     // 600 ms per half-cycle
                [](float t) { return t; }, // linear — easing handled below
                [this](float t) {
                    // Ping-pong: 0→1 on even passes, 1→0 on odd passes.
                    currentAlpha = pingPongPhase ? juce::jmap(t, 1.0f, 0.3f) : juce::jmap(t, 0.3f, 1.0f);
                    repaint();
                },
                [this]() {
                    pingPongPhase = !pingPongPhase;
                    // Only keep pulsing while waiting; caller sets visible(false) on stop.
                    if (isVisible() && updaterPtr != nullptr)
                        startPulse(*updaterPtr);
                });
        }

        void stopPulse(juce::VBlankAnimatorUpdater& updater) {
            updaterPtr = nullptr;
            pulseAnim.stop(updater);
            currentAlpha = 0.0f;
            repaint();
        }

    private:
        gravisynth::ui::AnimationDriver pulseAnim;
        juce::VBlankAnimatorUpdater* updaterPtr = nullptr;
        float currentAlpha = 0.3f;
        bool pingPongPhase = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpinnerDot)
    };
    // ----------------------------------------------------------------------

    AIIntegrationService& aiService;
    juce::ApplicationProperties& appProperties;
    bool isWaitingForResponse = false;

    juce::Viewport viewport;
    juce::Component messageList;
    juce::TextEditor inputField;
    juce::TextButton sendButton;
    juce::TextButton cancelButton; // Visible ONLY while waiting
    juce::TextButton newChatButton;
    juce::ComboBox modelPicker;

    // Pulse animation for the "AI is thinking" state indicator.
    // VBlankAnimatorUpdater is attached to this Component.
    juce::VBlankAnimatorUpdater vblankUpdater{this};
    SpinnerDot spinnerDot;

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
    void flushDebugLog();

    juce::CriticalSection logLock;
    juce::StringArray pendingLogLines;
    std::atomic<bool> logFlushScheduled{false};

public:
    void appendDebugLog(const juce::String& msg);

private:
#endif

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AIChatComponent)
};

} // namespace gsynth
