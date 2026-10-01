#pragma once

#include <juce_core/juce_core.h>

// ChatMessageAccessibilityText.h: what a screen reader says for one AI chat message, e.g.
// "You: make the bass wetter" or "Assistant: Done. Includes a patch you can apply." Pure, so the
// wording is unit-testable without a component or a native accessibility peer.

namespace synth::ui {

/** The speaker plus the message text. `role` is the chat role ("user" or "assistant"); anything but
 *  "user" is read as the assistant. `hasPatch` and `hasTimelineChanges` add a short note that the
 *  message carries something to apply, since the cards themselves are separate controls. */
inline juce::String describeChatMessageForAccessibility(const juce::String& role, const juce::String& text,
                                                        bool hasPatch, bool hasTimelineChanges) {
    juce::String result = (role == "user" ? "You: " : "Assistant: ") + text.trim();
    if (hasPatch)
        result += ". Includes a patch you can apply";
    if (hasTimelineChanges)
        result += ". Includes timeline changes you can apply";
    return result;
}

} // namespace synth::ui
