#pragma once

#include <juce_core/juce_core.h>

// ChatMessageAccessibilityText.h: what a screen reader says for one AI chat message, e.g.
// "You: make the bass wetter" or "Assistant: Done. Includes an edit plan you can apply." Pure, so the
// wording is unit-testable without a component or a native accessibility peer.

namespace synth::ui {

/** The speaker plus the message text. `role` is the chat role ("user" or "assistant"); anything but
 *  "user" is read as the assistant. `hasEditPlan` adds a short note that the message carries a plan
 *  that can be applied, since its card is a separate control. */
inline juce::String describeChatMessageForAccessibility(const juce::String& role, const juce::String& text,
                                                        bool hasEditPlan) {
    juce::String result = (role == "user" ? "You: " : "Assistant: ") + text.trim();
    if (hasEditPlan)
        result += ". Includes an edit plan you can apply";
    return result;
}

} // namespace synth::ui
