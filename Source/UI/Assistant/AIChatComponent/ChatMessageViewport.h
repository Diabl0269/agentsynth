#pragma once

#include "UI/Layout/FocusRing.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace synth {

// The AI chat's scrolling message list. It is a Tab stop (a juce::Viewport already scrolls with the
// arrow, Page and Home/End keys while focused), shows the accent focus ring, and tells a screen
// reader it is a list named "Chat messages" so each message bubble inside reads as one list item.
class ChatMessageViewport : public juce::Viewport {
public:
    ChatMessageViewport() {
        setTitle("Chat messages");
        setDescription("Arrow keys scroll through the conversation");
        setWantsKeyboardFocus(true);
    }

    void paintOverChildren(juce::Graphics& g) override {
        synth::ui::paintFocusRing(g, getLocalBounds().toFloat(), *this);
    }

    void focusGained(juce::Component::FocusChangeType) override { repaint(); }
    void focusLost(juce::Component::FocusChangeType) override { repaint(); }

    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override {
        return std::make_unique<juce::AccessibilityHandler>(*this, juce::AccessibilityRole::list);
    }
};

} // namespace synth
