#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// The one text link of the app ("Show all", "Show on canvas >"): a juce::Button that paints its text in the
// accent colour, brighter and underlined while hovered, dimmed while disabled. Looks are defined once, in
// AppLookAndFeel::drawTextLink. The text is also the accessible title; description, tooltip and focus
// policy stay with the call site.
class TextLinkButton : public juce::Button {
public:
    explicit TextLinkButton(const juce::String& text,
                            juce::Justification justification = juce::Justification::centredLeft);

    void setJustification(juce::Justification justification);
    juce::Justification getJustification() const noexcept { return justification_; }

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::Justification justification_;
};

} // namespace synth::ui
