#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace synth::ui {

// Padding around the link text: the focus ring is the text rectangle grown by these, and the text of a
// left- or right-justified link sits kTextLinkPadX in from its edge so the ring never cuts a glyph.
constexpr int kTextLinkPadX = 4;
constexpr int kTextLinkPadY = 2;
constexpr float kTextLinkFontHeight = 11.0f;

// The one font every text link paints and measures with.
inline juce::Font textLinkFont() { return juce::Font{juce::FontOptions(kTextLinkFontHeight)}; }

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

    // Narrowest width that shows the whole text with its ring pad on both sides.
    int getIdealWidth() const;

    void paintButton(juce::Graphics& g, bool highlighted, bool down) override;

private:
    juce::Justification justification_;
};

} // namespace synth::ui
