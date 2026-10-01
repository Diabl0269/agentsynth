#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// An accessibility handler whose help text (what VoiceOver reads after a pause) is the component's
// tooltip. JUCE's stock widgets do this themselves; a custom handler built on the base class reads
// only Component::getHelpText(), so a painted view's tooltip would never reach a screen reader.
class TooltipHelpHandler : public juce::AccessibilityHandler {
public:
    using juce::AccessibilityHandler::AccessibilityHandler;

    juce::String getHelp() const override {
        // TooltipClient::getTooltip() is non-const; reading it does not change the component.
        if (auto* client = dynamic_cast<juce::TooltipClient*>(const_cast<juce::Component*>(&getComponent())))
            return client->getTooltip();
        return juce::AccessibilityHandler::getHelp();
    }
};

} // namespace synth::ui
